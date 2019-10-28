#pragma once

#include <string>
#include <memory>

namespace Etcd {

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

}
