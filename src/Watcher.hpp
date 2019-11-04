#pragma once

#include "Etcd.hpp"
#include "Etcd/Logger.hpp"

#include "proto/rpc.grpc.pb.h" // no idea why, but this here is needed otherwise it won't compile
#include <grpcpp/channel.h>

namespace Etcd {

class Watcher
{
public:
    virtual ~Watcher() = default;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool AddPrefix(
        const std::string& prefix,
        Etcd::OnKeyAddedFunc onKeyAdded,
        Etcd::OnKeyRemovedFunc onKeyRemoved) = 0;
    virtual bool RemovePrefix(const std::string& prefix) = 0;

    static std::unique_ptr<Watcher> CreateV3(std::shared_ptr<grpc::Channel> channel, std::shared_ptr<Logger> logger);
};

}
