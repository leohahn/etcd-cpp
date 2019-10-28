#include "Etcd/Logger.hpp"

#include <memory>
#include <string>
#include <iostream>

namespace Etcd {

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

}
