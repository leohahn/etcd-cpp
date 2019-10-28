#pragma once

#include "Etcd/Logger.hpp"

#include <grpc++/grpc++.h>
#include <fmt/format.h>

#include <unordered_map>
#include <chrono>
#include <queue>

namespace Etcd {

using WatchStream = std::unique_ptr<grpc::ClientAsyncReaderWriter<etcdserverpb::WatchRequest, etcdserverpb::WatchResponse>>;

struct Listener
{
    Etcd::OnKeyAddedFunc onKeyAdded;
    Etcd::OnKeyRemovedFunc onKeyRemoved;

    Listener() = default;

    Listener(Etcd::OnKeyAddedFunc onKeyAdded, Etcd::OnKeyRemovedFunc onKeyRemoved)
        : onKeyAdded(std::move(onKeyAdded))
        , onKeyRemoved(std::move(onKeyRemoved))
    {}
};

enum class WatchTag
{
    Start = 1,
    Create = 2,
    Cancel = 3,
};

enum class WatchJobType
{
    Add,
    Remove,
    Unknown,
};

struct WatchData
{
    WatchTag tag;
    std::string prefix;
    Listener listener;
    int64_t watchId;

    // Called whenever the watch is created
    std::function<void()> onCreate;
    // Called whenever the watch is canceled
    std::function<void()> onCancel;

    WatchData(WatchTag t,
              const std::string& p = "",
              Listener l = Listener(),
              std::function<void()> onCreate = nullptr)
        : tag(t)
        , prefix(p)
        , listener(std::move(l))
        , watchId(-1)
        , onCreate(std::move(onCreate))
    {}
};

class Watcher
{
public:
    Watcher(std::shared_ptr<etcdserverpb::Watch::Stub> watchStub, std::shared_ptr<Logger> logger)
        : _watchStub(std::move(watchStub))
        , _logger(std::move(logger))
    {
    }

    bool Start()
    {
        assert(_watchThread == nullptr && "StartWatch may be called only once!");

        _watchStream = _watchStub->AsyncWatch(
            &_watchContext,
            &_watchCompletionQueue,
            reinterpret_cast<void*>(WatchTag::Start)
        );

        bool ok;
        void* tag;
        if (!_watchCompletionQueue.Next(&tag, &ok)) {
            _logger->Error("Failed to start watch: completion queue shutdown");
            return false;
        }

        if (!ok) {
            _logger->Error("Failed to start watch: failed to start stream");
            return false;
        }

        assert(tag != nullptr, "should not be null");
        assert((WatchTag)((int)tag) == WatchTag::Start);

        _logger->Info("Watch connection done!");

        _watchThread = std::unique_ptr<std::thread>(
            new std::thread(std::bind(&Watcher::Thread_Start, this))
        );
        return true;
    }

    void Stop()
    {
        if (_watchThread == nullptr) {
            return;
        }

        using namespace std::chrono;
        assert(_watchThread != nullptr && "You should call StartWatch() first!");

        _watchContext.TryCancel();

        _logger->Debug("Requesting watcher thread shutdown");
        _watchCompletionQueue.Shutdown();
        if (_watchThread->joinable()) {
            _watchThread->join();
        }
        _watchThread.reset();
    }

    void AddPrefix(
        const std::string& prefix,
        Etcd::OnKeyAddedFunc onKeyAdded,
        Etcd::OnKeyRemovedFunc onKeyRemoved,
        std::function<void()> onComplete)
    {
        assert(!prefix.empty() && "Prefix should not be empty");
        Listener listener(std::move(onKeyAdded), std::move(onKeyRemoved));
        CreateWatch(prefix, std::move(listener), std::move(onComplete));
    }

    bool RemovePrefix(const std::string& prefix, std::function<void()> onComplete)
    {
        assert(!prefix.empty() && "Prefix should not be empty");
        bool ok = CancelWatch(prefix, std::move(onComplete));
        return ok;
    }

private:
    void Thread_Start()
    {
        using namespace std::chrono;
        _logger->Info("Watcher thread started");

        for (;;) {
            void* tag;
            bool ok;
            if (!_watchCompletionQueue.Next(&tag, &ok)) {
                _logger->Info("Completion queue is completely drained and shut down, worker thread exiting");
                break;
            }

            // We got an event from the completion queue.
            // Now we see what type it is, process it and then delete the struct.
            assert(tag && "tag should not be null");
            WatchData* watchData = reinterpret_cast<WatchData*>(tag);

            if (!ok) {
                _watchContext.TryCancel();
                _watchCompletionQueue.Shutdown();
                continue;
            }

            switch (watchData->tag) {
                case WatchTag::Create: {
                    // The watch is not created yet, since the server has to confirm the creation.
                    // Therefore we place the watch data into a pending queue.
                    _pendingWatchCreateData.push(watchData);
                    break;
                }
                case WatchTag::Cancel: {
                    _logger->Info(fmt::format("Requesting watch cancel for prefix {}", watchData->prefix));
                    break;
                }
                default: {
                    assert(false && "should not enter here");
                    break;
                }
            }

            delete watchData;
        }
    }

    void CreateWatch(
        const std::string& prefix,
        Listener listener,
        std::function<void()> onComplete)
    {
        using namespace etcdserverpb;
        // We get the rangeEnd. We currently always treat the key as a prefix.
        std::string rangeEnd(prefix);
        int ascii = rangeEnd[prefix.size() - 1];
        rangeEnd.back() = ascii + 1;

        auto createReq = new WatchCreateRequest();
        createReq->set_key(prefix);
        createReq->set_range_end(rangeEnd);
        createReq->set_start_revision(0);      // TODO(lhahn): specify another revision here
        createReq->set_prev_kv(false);         // TODO(lhahn): consider other value here
        createReq->set_progress_notify(false); // TODO(lhahn): currently we do not support reconnection

        WatchRequest req;
        req.set_allocated_create_request(createReq);
        // We request to write to the server the watch create request
        auto watchData = new WatchData(WatchTag::Create, prefix, std::move(listener), std::move(onComplete));
        _watchStream->Write(req, watchData);
    }

    bool CancelWatch(const std::string& prefix, std::function<void()> onComplete)
    {
        using namespace etcdserverpb;

        WatchData* watchData;

        // First we try to remove the prefix from the created watches map
        {
            std::lock_guard<decltype(_watchThreadMutex)> lock(_watchThreadMutex);
            auto it = _createdWatches.find(prefix);
            if (it == _createdWatches.end()) {
                // We are trying to cancel a watch that does not exist yet.
                return false;
            }

            watchData = it->second;
            _createdWatches.erase(it);
        }

        assert(watchData != nullptr);

        // We change the current tag to cancel, otherwise we will not know
        // what to do once we get the struct from the completion queue.
        watchData->tag = WatchTag::Cancel;

        auto cancelReq = new WatchCancelRequest();
        cancelReq->set_watch_id(watchData->watchId);

        WatchRequest req;
        req.set_allocated_cancel_request(cancelReq);

        _watchStream->Write(req, watchData);
        return true;
    }

private:
    std::shared_ptr<etcdserverpb::Watch::Stub> _watchStub;
    std::shared_ptr<Logger> _logger;

    // Hold all of the watches that where actually created
    std::unordered_map<std::string, WatchData*> _createdWatches;

    // The worker thread is responsible for watching over etcd
    // and sending keep alive requests.
    std::mutex _watchThreadMutex;
    std::queue<WatchData*> _pendingWatchCreateData;
    std::unique_ptr<std::thread> _watchThread;

    grpc::CompletionQueue _watchCompletionQueue;
    grpc::ClientContext _watchContext;
    WatchStream _watchStream;
};

}
