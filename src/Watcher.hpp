#pragma once

#include "Etcd.hpp"
#include "Etcd/Logger.hpp"

#include "proto/rpc.grpc.pb.h"

namespace Etcd {

class Watcher
{
public:
    virtual ~Watcher() = default;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual void AddPrefix(
        const std::string& prefix,
        Etcd::OnKeyAddedFunc onKeyAdded,
        Etcd::OnKeyRemovedFunc onKeyRemoved,
        std::function<void()> onComplete) = 0;
    virtual bool RemovePrefix(const std::string& prefix, std::function<void()> onComplete) = 0;

    static std::unique_ptr<Watcher> CreateV3(std::shared_ptr<etcdserverpb::Watch::Stub> watchStub, std::shared_ptr<Logger> logger);
};

}
