#include "EtcdApi.hpp"

#include "proto/kv.grpc.pb.h"
#include "proto/rpc.grpc.pb.h"

#include <grpc++/grpc++.h>

class ClientV3 : public Etcd::Client
{
public:
    ClientV3(const std::string& address)
        : _channel(grpc::CreateChannel(address, grpc::InsecureChannelCredentials()))
    {
    }

    bool Put(const std::string& key, const std::string& value, Etcd::LeaseId leaseId = 0) override
    {
        // TODO: finish this method implementation
        auto putRequest = new etcdserverpb::PutRequest();
        putRequest->set_key(key);
        putRequest->set_value(value);
        putRequest->set_prev_kv(true);
        putRequest->set_lease(leaseId);

        return true;
    }

private:
    std::shared_ptr<grpc::Channel> _channel;
    std::shared_ptr<etcdserverpb::KV::Stub> _kvStub;
    std::shared_ptr<etcdserverpb::Watch::Stub> _watchStub;
    std::shared_ptr<etcdserverpb::Lease::Stub> _leaseStub;
};

static std::shared_ptr<Etcd::Client>
CreateV3(const std::string& address)
{
    return std::make_shared<ClientV3>(address);
}