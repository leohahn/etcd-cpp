#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <chrono>
#include <vector>
#include <functional>

#include "Etcd/Logger.hpp"

// FIXME(leo): this is a lazy copy paste from grpc enums.
#define ETCD_STATUS_CODES \
    ETCD_STATUS_CODE(Ok = 0, "Ok"),\
    ETCD_STATUS_CODE(Cancelled = 1, "Cancelled"),\
    ETCD_STATUS_CODE(Unknown = 2, "Unknown"),\
    ETCD_STATUS_CODE(InvalidArgument = 3, "InvalidArgument"),\
    ETCD_STATUS_CODE(DeadlineExceeded = 4, "DeadlineExceeded"),\
    ETCD_STATUS_CODE(NotFound = 5, "NotFound"),\
    ETCD_STATUS_CODE(AlreadyExists = 6, "AlreadyExists"),\
    ETCD_STATUS_CODE(PermissionDenied = 7, "PermissionDenied"),\
    ETCD_STATUS_CODE(Unauthenticated = 16, "Unauthenticated"),\
    ETCD_STATUS_CODE(ResourceExhausted = 8, "ResourceExhausted"),\
    ETCD_STATUS_CODE(FailedPrecondition = 9, "FailedPrecondition"),\
    ETCD_STATUS_CODE(Aborted = 10, "Aborted"),\
    ETCD_STATUS_CODE(OutOfRange = 11, "OutOfRange"),\
    ETCD_STATUS_CODE(Unimplemented = 12, "Unimplemented"),\
    ETCD_STATUS_CODE(Internal = 13, "Internal"),\
    ETCD_STATUS_CODE(Unavailable = 14, "Unavailable"),\
    ETCD_STATUS_CODE(DataLoss = 15, "DataLoss"),

namespace Etcd
{

using LeaseId = int64_t;

// This is copied from gRPC in order to hide the dependency from the user.
enum class StatusCode : int
{
#define ETCD_STATUS_CODE(e, s) e
    ETCD_STATUS_CODES
#undef ETCD_STATUS_CODE
};

const char* StatusCodeStr(StatusCode code);

struct LeaseGrantResponse
{
    StatusCode statusCode;
    LeaseId id;
    std::chrono::seconds ttl;

    LeaseGrantResponse(StatusCode code, LeaseId id, std::chrono::seconds ttl)
        : statusCode(code)
        , id(id)
        , ttl(ttl)
    {}

    bool IsOk() const { return statusCode == StatusCode::Ok; }
};

struct GetResponse
{
    StatusCode statusCode;
    std::string value;

    GetResponse(StatusCode code, const std::string val)
        : statusCode(code)
        , value(val)
    {}

    explicit GetResponse(StatusCode code)
        : statusCode(code)
    {}

    bool IsOk() const { return statusCode == StatusCode::Ok; }
};

struct ListResponse
{
    using KeyValuePairs = std::vector<std::pair<std::string, std::string>>;
    StatusCode statusCode;
    KeyValuePairs kvs;

    ListResponse(StatusCode code, const KeyValuePairs& kvs)
        : statusCode(code)
        , kvs(kvs)
    {}

    explicit ListResponse(StatusCode code)
        : statusCode(code)
    {}

    bool IsOk() const { return statusCode == StatusCode::Ok; }
};

using OnKeyAddedFunc = std::function<void(const std::string&, const std::string&)>;
using OnKeyRemovedFunc = std::function<void(const std::string&)>;

class Client
{
public:
    virtual ~Client() = default;
    virtual bool TryConnect(std::chrono::milliseconds timeout) = 0;
    virtual StatusCode Put(const std::string& key, const std::string& value, LeaseId leaseId = 0) = 0;
    virtual StatusCode Delete(const std::string& key) = 0;
    virtual LeaseGrantResponse LeaseGrant(std::chrono::seconds ttl) = 0;
    virtual StatusCode LeaseRevoke(LeaseId leaseId) = 0;
    virtual GetResponse Get(const std::string& key) = 0;
    virtual ListResponse List(const std::string& keyPrefix = "") = 0;

    virtual bool StartWatch() = 0;
    virtual bool AddWatchPrefix(const std::string& prefix, OnKeyAddedFunc onKeyAdded, OnKeyRemovedFunc onKeyRemoved) = 0;
    virtual bool RemoveWatchPrefix(const std::string& prefix) = 0;
    virtual void StopWatch() = 0;

    // Creates the v3 implementation for this interface
    static std::shared_ptr<Client> CreateV3(const std::string& address, const std::shared_ptr<Etcd::Logger>& logger);

protected:
    Client(const std::shared_ptr<Logger>& logger)
        : _logger(logger)
    {}

protected:
    std::shared_ptr<Logger> _logger;
};

}
