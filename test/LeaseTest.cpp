#include <gtest/gtest.h>
#include <Etcd.hpp>

using namespace Etcd;

static constexpr const char* kAddress = "http://127.0.0.1:2379";

TEST(Lease, GrantWorks)
{
    auto logger = Etcd::Logger::CreateStdout();
    std::shared_ptr<Client> client = Client::CreateV3(kAddress, logger);
    LeaseGrantResponse res = client->LeaseGrant(std::chrono::seconds(5));
    EXPECT_TRUE(res.IsOk());
}