#include <gtest/gtest.h>
#include <Etcd.hpp>
#include <array>

using namespace Etcd;

static constexpr const char* kAddress = "http://127.0.0.1:2379";

TEST(KV, PutGetAndDeleteWorks)
{
    auto logger = Logger::CreateNull();
    auto client = Client::CreateV3(kAddress, logger);

    StatusCode code;

    code = client->Put("my_key_1", "my value");
    EXPECT_EQ(code, StatusCode::Ok);

    {
        auto getResponse = client->Get("my_key_1");
        EXPECT_TRUE(getResponse.IsOk());
        EXPECT_EQ(getResponse.value, "my value");
    }

    code = client->Delete("my_key_1");
    EXPECT_EQ(code, StatusCode::Ok);

    {
        auto getResponse = client->Get("my_key_1");
        EXPECT_EQ(getResponse.statusCode, StatusCode::NotFound);
    }
}
