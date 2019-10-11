#include <gtest/gtest.h>
#include <Etcd.hpp>
#include <array>

using namespace Etcd;

static constexpr const char* kAddress = "http://127.0.0.1:2379";

TEST(KV, PutAndDeleteWorks)
{
    auto logger = Logger::CreateNull();
    auto client = Client::CreateV3(kAddress, logger);

    StatusCode code;

    code = client->Put("my_key_1", "my value");
    EXPECT_EQ(code, StatusCode::Ok);

    code = client->Delete("my_key_1");
    EXPECT_EQ(code, StatusCode::Ok);
}
