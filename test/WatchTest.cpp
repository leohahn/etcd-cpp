#include <gtest/gtest.h>
#include <Etcd.hpp>
#include <array>

using namespace Etcd;

static constexpr const char* kAddress = "http://127.0.0.1:2379";

TEST(Watch, CanStartAndStop)
{
    auto logger = Etcd::Logger::CreateStdout();
    std::shared_ptr<Client> client = Client::CreateV3(kAddress, logger);
    bool ok = client->StartWatch();
    ASSERT_TRUE(ok);
    client->StopWatch();
}

