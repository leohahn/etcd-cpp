#pragma once

#include <string>
#include <cstdint>
#include <memory>

namespace Etcd
{

using LeaseId = int64_t;

class Client
{
public:
    virtual ~Client() = default;
    virtual bool Put(const std::string& key, const std::string& value, LeaseId leaseId = 0) = 0;

    static std::shared_ptr<Client> CreateV3(const std::string& address);
};

}