#include "Etcd.hpp"

#include "proto/kv.grpc.pb.h"
#include "proto/rpc.grpc.pb.h"

#include <cassert>
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <grpc++/grpc++.h>
#include <fmt/format.h>

static const char* kStatusCodeNames[] = {
#define ETCD_STATUS_CODE(e, s) s
    ETCD_STATUS_CODES
#undef ETCD_STATUS_CODE
};

const char*
Etcd::StatusCodeStr(StatusCode code)
{
    return kStatusCodeNames[code];
}

std::shared_ptr<grpc::Channel>
MakeChannel(const std::string& address, const std::shared_ptr<grpc::ChannelCredentials>& channelCredentials)
{
    const std::string substr("://");
    const auto i = address.find(substr);
    if (i == std::string::npos) {
        return grpc::CreateChannel(address, channelCredentials);
    }
    const std::string stripped_address = address.substr(i + substr.length());
    return grpc::CreateChannel(stripped_address, channelCredentials);
}

enum class WatchTag
{
    Start,
    Create,
    Cancel,
    WritesDone,
    Finish,
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
    std::unique_ptr<Etcd::WatchListener> listener;
    int64_t watchId;

    WatchData(WatchTag t, const std::string& p = "", std::unique_ptr<Etcd::WatchListener> l = nullptr)
        : tag(t)
        , prefix(p)
        , listener(std::move(l))
        , watchId(-1)
    {}
};

class ClientV3 : public Etcd::Client
{
    using WatchStream = std::unique_ptr<grpc::ClientAsyncReaderWriter<etcdserverpb::WatchRequest, etcdserverpb::WatchResponse>>;
public:

    ClientV3(const std::string& address, const std::shared_ptr<Etcd::Logger>& logger)
        : Client(logger)
        // TODO(lhahn): consider not using an insecure connection here
        , _channel(MakeChannel(address, grpc::InsecureChannelCredentials()))
        , _kvStub(etcdserverpb::KV::NewStub(_channel))
        , _watchStub(etcdserverpb::Watch::NewStub(_channel))
        , _leaseStub(etcdserverpb::Lease::NewStub(_channel))
    {
        assert(_channel != nullptr && "Channel should be valid");
        assert(_kvStub != nullptr && "KV stub should be valid");
        assert(_watchStub != nullptr && "Watch stub should be valid");
        assert(_leaseStub != nullptr && "Lease stub should be valid");
    }

    ~ClientV3()
    {
        assert(_watchThread == nullptr && "You should call StopWatch()!");
        _logger->Info("Client is being destroyed");
    }

    bool TryConnect(std::chrono::milliseconds timeout) override
    {
        assert(_channel && "channel should be valid");
        using namespace std::chrono;
        if (timeout.count() > 0) {
            // Wait for timeout until connected
            if (!_channel->WaitForConnected(system_clock::now() + timeout)) {
                _logger->Error("Failed to connect to etcd client");
                return false;
            }
        }
        return true;
    }

    Etcd::StatusCode Put(const std::string& key, const std::string& value, Etcd::LeaseId leaseId = 0) override
    {
        // TODO: finish this method implementation
        etcdserverpb::PutRequest putRequest;
        putRequest.set_key(key);
        putRequest.set_value(value);
        putRequest.set_lease(leaseId);
        putRequest.set_prev_kv(false); // do we need to get the previous value? (ignoring for now...)

        etcdserverpb::PutResponse putResponse;

        grpc::ClientContext context;
        grpc::Status status = _kvStub->Put(&context, putRequest, &putResponse);

        if (!status.ok()) {
            // TODO(leo): consider making a map here, in order to avoid a possible issue
            // where the values of the enum values change in a future version of gRPC.
            _logger->Error(fmt::format("Failed put request: {}, details: {}",
                           status.error_message(), status.error_details()));
            return (Etcd::StatusCode)status.error_code();
        }

        return Etcd::StatusCode::Ok;
    }

    Etcd::LeaseGrantResponse LeaseGrant(std::chrono::seconds ttl) override
    {
        etcdserverpb::LeaseGrantRequest req;
        req.set_ttl(ttl.count());
        etcdserverpb::LeaseGrantResponse res;

        grpc::ClientContext context;
        grpc::Status status = _leaseStub->LeaseGrant(&context, req, &res);

        if (!status.ok()) {
            auto ret = Etcd::LeaseGrantResponse(
                // TODO(leo): consider making a map here, in order to avoid a possible issue
                // where the values of the enum values change in a future version of gRPC.
                (Etcd::StatusCode)status.error_code(),
                res.id(),
                std::chrono::seconds(res.ttl())
            );

            _logger->Error(fmt::format("Failed lease grant request({}) {}, details: {}",
                           StatusCodeStr(ret.statusCode), status.error_message(), status.error_details()));
            return ret;
        }

        return Etcd::LeaseGrantResponse(
            Etcd::StatusCode::Ok,
            res.id(),
            std::chrono::seconds(res.ttl())
        );
    }

    Etcd::StatusCode LeaseRevoke(Etcd::LeaseId leaseId) override
    {
        etcdserverpb::LeaseRevokeRequest req;
        req.set_id(leaseId);

        etcdserverpb::LeaseRevokeResponse res;

        grpc::ClientContext context;
        grpc::Status status = _leaseStub->LeaseRevoke(&context, req, &res);

        if (!status.ok()) {
            auto ret = (Etcd::StatusCode)status.error_code();
            _logger->Error(fmt::format("Failed lease revoke request({}) {}, details: {}",
                           StatusCodeStr(ret), status.error_message(), status.error_details()));
            return ret;
        }

        return Etcd::StatusCode::Ok;
    }

    Etcd::GetResponse Get(const std::string& key) override
    {
        etcdserverpb::RangeRequest req;
        req.set_key(key);

        etcdserverpb::RangeResponse res;

        grpc::ClientContext context;
        grpc::Status status = _kvStub->Range(&context, req, &res);

        auto statusCode = (Etcd::StatusCode)status.error_code();

        if (!status.ok()) {
            Etcd::GetResponse ret(statusCode);
            _logger->Error(fmt::format("Failed range request({}) {}, details: {}",
                           Etcd::StatusCodeStr(statusCode), status.error_message(), status.error_details()));
            return ret;
        }

        if (res.count() == 0) {
            return Etcd::GetResponse(Etcd::StatusCode::NotFound);
        }

        assert(res.count() == 1 && "Only one key should be matched");
        assert(res.more() == false);
        assert(res.kvs(0).key() == key && "Key should be the same");
        return Etcd::GetResponse(statusCode, res.kvs(0).value());
    }

    Etcd::StatusCode Delete(const std::string& key) override
    {
        etcdserverpb::DeleteRangeRequest req;
        req.set_key(key);
        req.set_prev_kv(false); // TODO: consider adding true here

        etcdserverpb::DeleteRangeResponse res;

        grpc::ClientContext context;
        grpc::Status status = _kvStub->DeleteRange(&context, req, &res);

        if (!status.ok()) {
            auto ret = (Etcd::StatusCode)status.error_code();
            _logger->Error(fmt::format("Failed delete range request({}) {}, details: {}",
                           StatusCodeStr(ret), status.error_message(), status.error_details()));
            return ret;
        }

        return Etcd::StatusCode::Ok;
    }

    Etcd::ListResponse List(const std::string& keyPrefix) override
    {
        etcdserverpb::RangeRequest req;
        req.set_key(keyPrefix);

        etcdserverpb::RangeResponse res;

        grpc::ClientContext context;
        grpc::Status status = _kvStub->Range(&context, req, &res);

        auto statusCode = (Etcd::StatusCode)status.error_code();

        if (!status.ok()) {
            Etcd::ListResponse ret(statusCode);
            _logger->Error(fmt::format("Failed range request({}) {}, details: {}",
                           Etcd::StatusCodeStr(statusCode), status.error_message(), status.error_details()));
            return ret;
        }

        assert(res.more() == false);

        Etcd::ListResponse::KeyValuePairs kvs;
        kvs.reserve(res.count());

        for (int i = 0; i < res.count(); ++i) {
            const auto& kv = res.kvs(i);
            kvs.push_back(std::make_pair(kv.key(), kv.value()));
        }

        return Etcd::ListResponse(statusCode, std::move(kvs));
    }

    void StartWatch() override
    {
        assert(_watchThread == nullptr && "StartWatch may be called only once!");

        _watchStream = _watchStub->AsyncWatch(
            &_watchContext,
            &_watchCompletionQueue,
            reinterpret_cast<void*>(new WatchData(WatchTag::Start))
        );
        _watchThread = std::unique_ptr<std::thread>(
            new std::thread(std::bind(&ClientV3::WatcherThreadStart, this))
        );
    }

    void StopWatch() override
    {
        assert(_watchThread != nullptr && "You should call StartWatch() first!");

        _logger->Debug("Requesting watcher thread shutdown");
        _watchCompletionQueue.Shutdown();
        if (_watchThread->joinable()) {
            _watchThread->join();
        }
    }

    void AddWatchPrefix(const std::string& prefix, std::unique_ptr<Etcd::WatchListener> listener) override
    {
        assert(!prefix.empty() && "Prefix should not be empty");
        CreateWatch(prefix, std::move(listener));
    }

    void RemoveWatchPrefix(const std::string& prefix) override
    {
        assert(!prefix.empty() && "Prefix should not be empty");
        CancelWatch(prefix);
    }

    // Entrypoint for worker thread
    void WatcherThreadStart()
    {
        using namespace std::chrono;
        _logger->Info("Watcher thread started");

        for (;;) {
            // This boolean tracks whether the thread should exit or not.
            bool shouldExit = false;

            void* tag = nullptr;
            bool ok;
            bool gotEvent = _watchCompletionQueue.Next(&tag, &ok);

            if (!gotEvent) {
                _logger->Info("Completion queue is completely drained and shut down, worker thread exiting");
                break;
            }

            // We got an event from the completion queue.
            // Now we see what type it is, process it and then delete the struct.
            assert(tag && "tag should not be null");
            WatchData* watchData = reinterpret_cast<WatchData*>(tag);

            if (!ok) {
                _logger->Error(fmt::format(
                    "WatchTag of type {} will not be sent: prefix = {}",
                    watchData->tag,
                    watchData->prefix
                ));
                delete watchData;
                // Something went wrong, so we ignore this tag.
                continue;
            }

            switch (watchData->tag) {
                case WatchTag::Start: {
                    _logger->Info("Watch connection done!");
                    break;
                }
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
                case WatchTag::WritesDone: {
                    _logger->Info("Client finished writing to stream");
                    break;
                }
                case WatchTag::Finish: {
                    _logger->Info("Completion queue finished");
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

private:
    void CreateWatch(const std::string& prefix, std::unique_ptr<Etcd::WatchListener> listener)
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
        auto watchData = new WatchData(WatchTag::Create, prefix, std::move(listener));
        _watchStream->Write(req, watchData);
    }

    void CancelWatch(const std::string& prefix)
    {
        // TODO: implement it
    }

private:
    std::shared_ptr<grpc::Channel> _channel;
    std::shared_ptr<etcdserverpb::KV::Stub> _kvStub;
    std::shared_ptr<etcdserverpb::Watch::Stub> _watchStub;
    std::shared_ptr<etcdserverpb::Lease::Stub> _leaseStub;

    // Hold all of the watches that where actually created
    std::unordered_map<std::string, WatchData*> _createdWatches;

    // The worker thread is responsible for watching over etcd
    // and sending keep alive requests.
    std::mutex _watchThreadMutex;
    std::queue<WatchData*> _pendingWatchCreateData;
    WatchStream _watchStream;
    std::unique_ptr<std::thread> _watchThread;

    grpc::ClientContext _watchContext;
    grpc::CompletionQueue _watchCompletionQueue;
};

std::shared_ptr<Etcd::Client>
Etcd::Client::CreateV3(const std::string& address, const std::shared_ptr<Etcd::Logger>& logger)
{
    assert(logger != nullptr && "Logger should be valid");
    return std::make_shared<ClientV3>(address, logger);
}

//----------------------------------------
// Loggers
//----------------------------------------
class StdoutLogger : public Etcd::Logger
{
public:
    void Error(const std::string& str) override { std::cerr << str << "\n"; }
    void Warn(const std::string& str) override { std::cout << str << "\n"; }
    void Info(const std::string& str) override { std::cout << str << "\n"; }
    void Debug(const std::string& str) override { std::cout << str << "\n"; }
};

std::shared_ptr<Etcd::Logger>
Etcd::Logger::CreateStdout()
{
    return std::make_shared<StdoutLogger>();
}

class NullLogger : public Etcd::Logger
{
public:
    void Error(const std::string& str) override {(void)str;}
    void Warn(const std::string& str) override {(void)str;}
    void Info(const std::string& str) override {(void)str;}
    void Debug(const std::string& str) override {(void)str;}
};

std::shared_ptr<Etcd::Logger>
Etcd::Logger::CreateNull()
{
    return std::make_shared<NullLogger>();
}
