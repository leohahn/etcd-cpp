#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <chrono>

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

// This logger class implementation should be thread safe
class Logger
{
public:
    virtual ~Logger() = default;
    virtual void Error(const std::string& str) = 0;
    virtual void Warn(const std::string& str) = 0;
    virtual void Info(const std::string& str) = 0;
    virtual void Debug(const std::string& str) = 0;

    static std::shared_ptr<Logger> CreateStdout();
    static std::shared_ptr<Logger> CreateNull();
};

using LeaseId = int64_t;

// This is copied from gRPC in order to hide the dependency from the user.
enum StatusCode
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
    LeaseId id;
    std::chrono::seconds ttl;

    GetResponse(StatusCode code, LeaseId id, std::chrono::seconds ttl)
        : statusCode(code)
        , id(id)
        , ttl(ttl)
    {}

    bool IsOk() const { return statusCode == StatusCode::Ok; }
};

class Client
{
public:
    virtual ~Client() = default;
    virtual bool TryConnect(std::chrono::milliseconds timeout) = 0;
    virtual StatusCode Put(const std::string& key, const std::string& value, LeaseId leaseId = 0) = 0;
    virtual StatusCode Delete(const std::string& key) = 0;
    virtual LeaseGrantResponse LeaseGrant(std::chrono::seconds ttl) = 0;
    virtual StatusCode LeaseRevoke(LeaseId leaseId) = 0;
    //virtual GetResponse Get(const std::string& str) = 0;

    static std::shared_ptr<Client> CreateV3(const std::string& address, const std::shared_ptr<Etcd::Logger>& logger);

protected:
    Client(const std::shared_ptr<Logger>& logger)
        : _logger(logger)
    {}

protected:
    std::shared_ptr<Logger> _logger;
};

}
