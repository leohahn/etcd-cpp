#include <gtest/gtest.h>
#include <Etcd.hpp>

using namespace Etcd;

static constexpr const char* kAddress = "127.0.0.1:2379";

TEST(Lease, GrantWorks)
{
    auto logger = Etcd::Logger::CreateStdout();
    std::shared_ptr<Client> client = Client::CreateV3(kAddress, logger);

    //ASSERT_TRUE(client->TryConnect(std::chrono::seconds(1)));

    LeaseGrantResponse res = client->LeaseGrant(std::chrono::seconds(5));

    logger->Info("Status: " + std::string(Etcd::StatusCodeStr(res.statusCode)));

    EXPECT_TRUE(res.IsOk());
}