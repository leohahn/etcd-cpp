#include <gtest/gtest.h>
#include <Etcd.hpp>
#include <array>

using namespace Etcd;

static constexpr const char* kAddress = "http://127.0.0.1:2379";

TEST(Lease, GrantWorks)
{
    auto logger = Etcd::Logger::CreateNull();
    std::shared_ptr<Client> client = Client::CreateV3(kAddress, logger);
    LeaseGrantResponse res = client->LeaseGrant(std::chrono::seconds(5));
    EXPECT_TRUE(res.IsOk());
}

TEST(Lease, RevokeFailsIfLeaseDoesNoExist)
{
    auto logger = Etcd::Logger::CreateNull();
    std::shared_ptr<Client> client = Client::CreateV3(kAddress, logger);

    std::array<LeaseId, 5> invalidLeaseIds = {
        12093192039012, 12312590384590, 91923, 101, 20
    };

    for (LeaseId leaseId : invalidLeaseIds) {
        StatusCode res = client->LeaseRevoke(leaseId);
        EXPECT_EQ(res, StatusCode::NotFound);
    }
}
