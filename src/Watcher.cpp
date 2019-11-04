#include "Watcher.hpp"

#include <grpc++/grpc++.h>
#include <fmt/format.h>

#include "proto/etcdserver.grpc.pb.h"
#include "proto/rpc.grpc.pb.h"

#include <algorithm>
#include <queue>
#include <thread>
#include <vector>
#include <future>
#include <atomic>

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
    Read = 4,
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

class WatcherV3 : public Watcher
{
public:
    WatcherV3(std::shared_ptr<grpc::Channel> channel, std::shared_ptr<Logger> logger)
        : _channel(channel)
        , _logger(std::move(logger))
        , _pendingWatchCreateData(nullptr)
        , _pendingWatchCancelData(nullptr)
        , _threadRunning(false)
    {
    }

    bool Start() override
    {
        if (_threadRunning.load()) {
            _logger->Error("StartWatch may be called only once!");
            return false;
        }

        if (_channel->GetState(false) == grpc_connectivity_state::GRPC_CHANNEL_SHUTDOWN) {
            _logger->Error("Cannot start watch, since channel was shutdown");
            return false;
        }

        _watchStub = etcdserverpb::Watch::NewStub(_channel);
        //_watchCompletionQueue = grpc::CompletionQueue();
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

        // Start reading from the watch stream
        auto watchData = new WatchData(WatchTag::Read);
        _watchStream->Read(&_watchResponse, static_cast<void*>(watchData));

        _threadRunning.store(true);
        _watchThread = std::thread(std::bind(&WatcherV3::Thread_Start, this));
        return true;
    }

    void Stop() override
    {
        using namespace std::chrono;
        if (!_threadRunning.load()) {
            return;
        }

        _watchContext.TryCancel();

        _logger->Debug("Requesting watcher thread shutdown");
        _watchCompletionQueue.Shutdown();
        if (_watchThread.joinable()) {
            _watchThread.join();
        }
        _watchStub = nullptr;
    }

    bool AddPrefix(
        const std::string& prefix,
        Etcd::OnKeyAddedFunc onKeyAdded,
        Etcd::OnKeyRemovedFunc onKeyRemoved) override
    {
        assert(!prefix.empty() && "Prefix should not be empty");
        if (!_threadRunning.load()) {
            _logger->Error(fmt::format("cannot add watch prefix {}, worker thread exited", prefix));
            return false;
        }
        Listener listener(std::move(onKeyAdded), std::move(onKeyRemoved));
        return CreateWatch(prefix, std::move(listener));
    }

    bool RemovePrefix(const std::string& prefix) override
    {
        assert(!prefix.empty() && "Prefix should not be empty");
        if (!_threadRunning.load()) {
            _logger->Error(fmt::format("cannot remove watch prefix {}, worker thread exited", prefix));
            return false;
        }
        return CancelWatch(prefix);
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

            if (!ok) {
                _watchContext.TryCancel();
                _watchCompletionQueue.Shutdown();
                continue;
            }

            WatchData* watchData = reinterpret_cast<WatchData*>(tag);

            switch (watchData->tag) {
                case WatchTag::Create: {
                    // The watch is not created yet, since the server has to confirm the creation.
                    // Therefore we place the watch data into a pending queue.
                    _pendingWatchCreateData = watchData;
                    break;
                }
                case WatchTag::Cancel: {
                    _logger->Info(fmt::format("Requesting watch cancel for prefix {}", watchData->prefix));
                    break;
                }
                case WatchTag::Read: {
                    if (_watchResponse.created()) {
                        // Watch was created
                        assert(_pendingWatchCreateData && "should've a pending watch create data");

                        _pendingWatchCreateData->watchId = _watchResponse.watch_id();

                        _logger->Debug(fmt::format(
                            "watch id {} created (prefix = {})",
                            _pendingWatchCreateData->watchId,
                            _pendingWatchCreateData->prefix));

                        assert(_pendingWatchCreateData->onCreate != nullptr && "should not be nullptr");
                        _pendingWatchCreateData->onCreate();
                        _pendingWatchCreateData->onCreate = nullptr;

                        std::lock_guard<decltype(_watchThreadMutex)> lock(_watchThreadMutex);
                        _createdWatches.push_back(_pendingWatchCreateData);
                        _pendingWatchCreateData = nullptr;
                    } else if (_watchResponse.canceled()) {
                        assert(_pendingWatchCancelData && "shoul've a pending watch cancel data");
                        assert(_pendingWatchCancelData->watchId == _watchResponse.watch_id());

                        _logger->Debug(fmt::format(
                            "watch id {} canceled (prefix = {})",
                            _pendingWatchCancelData->watchId,
                            _pendingWatchCancelData->prefix));

                        _pendingWatchCancelData->onCancel();
                        delete _pendingWatchCancelData;
                        _pendingWatchCancelData = nullptr;
                    } else {
                        auto events = _watchResponse.events();
                        auto createdWatchData = FindCreatedWatchWithLock(_watchResponse.watch_id());
                        assert(createdWatchData && "watch data should exist");

                        _logger->Debug(fmt::format("received {} watch events", events.size()));

                        // Loop over all events and call the respective listener
                        for (const auto& ev : events) {
                            if (ev.type() == mvccpb::Event_EventType_PUT) {
                                createdWatchData->listener.onKeyAdded(ev.kv().key(), ev.kv().value());
                            } else {
                                assert(ev.type() == mvccpb::Event_EventType_DELETE);
                                createdWatchData->listener.onKeyRemoved(ev.kv().key());
                            }
                        }
                    }

                    // We read again from the server after we got a message.
                    _watchStream->Read(&_watchResponse, watchData);
                    break;
                }
                default: {
                    assert(false && "should not enter here");
                    break;
                }
            }
        }

        _logger->Debug("Watcher thread stopped running");
        _threadRunning.store(false);
    }

    bool CreateWatch(const std::string& prefix, Listener listener)
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

        auto createPromise = std::make_shared<std::promise<bool>>();
        std::future<bool> createFuture = createPromise->get_future();

        auto watchData = new WatchData(WatchTag::Create, prefix, std::move(listener), [createPromise]() {
            createPromise->set_value(true);
        });

        _watchStream->Write(req, watchData);

        auto status = createFuture.wait_for(std::chrono::seconds(1));
        if (status == std::future_status::timeout) {
            return false;
        }

        return true;
    }

    bool CancelWatch(const std::string& prefix)
    {
        using namespace etcdserverpb;

        // First we try to remove the prefix from the created watches map
        {
            std::lock_guard<decltype(_watchThreadMutex)> lock(_watchThreadMutex);
            auto it = std::find_if(_createdWatches.begin(), _createdWatches.end(), [&](const WatchData* data) -> bool {
                return data->prefix == prefix;
            });
            if (it == _createdWatches.end()) {
                // We are trying to cancel a watch that does not exist yet.
                _logger->Error(fmt::format("watch prefix {} does not exist", prefix));
                return false;
            }
            _pendingWatchCancelData = *it;
            _createdWatches.erase(it);
        }

        assert(_pendingWatchCancelData != nullptr);

        auto cancelPromise = std::make_shared<std::promise<void>>();
        std::future<void> cancelFuture = cancelPromise->get_future();

        // We change the current tag to cancel, otherwise we will not know
        // what to do once we get the struct from the completion queue.
        _pendingWatchCancelData->tag = WatchTag::Cancel;
        _pendingWatchCancelData->onCancel = [=]() {
            cancelPromise->set_value();
        };

        auto cancelReq = new WatchCancelRequest();
        cancelReq->set_watch_id(_pendingWatchCancelData->watchId);

        WatchRequest req;
        req.set_allocated_cancel_request(cancelReq);

        _watchStream->Write(req, _pendingWatchCancelData);

        auto status = cancelFuture.wait_for(std::chrono::seconds(1));
        if (status == std::future_status::timeout) {
            return false;
        }

        return true;
    }

    WatchData* FindCreatedWatchWithLock(int64_t watchId)
    {
        std::lock_guard<decltype(_watchThreadMutex)> lock(_watchThreadMutex);
        auto it = std::find_if(_createdWatches.begin(), _createdWatches.end(), [=](const WatchData* data) -> bool {
            return data->watchId == watchId;
        });
        if (it == _createdWatches.end()) {
            return nullptr;
        } else { 
            return *it;
        }
    }

private:
    std::shared_ptr<grpc::Channel> _channel;
    std::shared_ptr<etcdserverpb::Watch::Stub> _watchStub;
    std::shared_ptr<Logger> _logger;
    etcdserverpb::WatchResponse _watchResponse;

    // Hold all of the watches that where actually created
    std::vector<WatchData*> _createdWatches;

    // The worker thread is responsible for watching over etcd
    // and sending keep alive requests.
    std::mutex _watchThreadMutex;
    WatchData* _pendingWatchCreateData;
    WatchData* _pendingWatchCancelData;
    std::thread _watchThread;
    std::atomic_bool _threadRunning;

    grpc::CompletionQueue _watchCompletionQueue;
    grpc::ClientContext _watchContext;
    WatchStream _watchStream;
};

std::unique_ptr<Watcher>
Watcher::CreateV3(std::shared_ptr<grpc::Channel> channel, std::shared_ptr<Logger> logger)
{
    return std::unique_ptr<Watcher>(new WatcherV3(std::move(channel), std::move(logger)));
}

}
