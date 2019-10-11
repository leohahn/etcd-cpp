#include "Etcd.hpp"

#include "proto/kv.grpc.pb.h"
#include "proto/rpc.grpc.pb.h"

#include <grpc++/grpc++.h>
#include <cassert>
#include <iostream>
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

class ClientV3 : public Etcd::Client
{
public:
    ClientV3(const std::string& address, const std::shared_ptr<Etcd::Logger>& logger)
        : Client(logger)
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
            _logger->Error(fmt::format("Failed delete range request({}) {}, details: {}",
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

private:
    std::shared_ptr<grpc::Channel> _channel;
    std::shared_ptr<etcdserverpb::KV::Stub> _kvStub;
    std::shared_ptr<etcdserverpb::Watch::Stub> _watchStub;
    std::shared_ptr<etcdserverpb::Lease::Stub> _leaseStub;
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
