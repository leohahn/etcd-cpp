#include <gtest/gtest.h>
#include <Etcd.hpp>
#include <unordered_map>

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

TEST(KV, ListAllWorks)
{
    using namespace std;
    auto logger = Logger::CreateNull();
    auto client = Client::CreateV3(kAddress, logger);

    unordered_map<string, string> kvsToAdd = {
        {"my_key_0", "my_val_0"},
        {"my_key_1", "my_val_1"},
        {"my_key_2", "my_val_2"},
        {"my_key_3", "my_val_3"},
        {"my_key_4", "my_val_4"},
        {"my_key_5", "my_val_5"},
        {"my_key_6", "my_val_6"},
        {"my_key_7", "my_val_7"},
        {"my_key_8", "my_val_8"},
        {"my_key_9", "my_val_9"},
    };

    for (const auto& kv : kvsToAdd) {
        client->Put(kv.first, kv.second);
    }

    ListResponse lsRes = client->List();

    for (const auto& returnedKv : lsRes.kvs) {
        auto it = kvsToAdd.find(returnedKv.first);
        EXPECT_NE(it, kvsToAdd.end());
        EXPECT_EQ(it->first, returnedKv.first);
        EXPECT_EQ(it->second, returnedKv.second);
    }

    // Delete all of the created keys
    for (const auto& kv : kvsToAdd) {
        StatusCode code = client->Delete(kv.first);
        EXPECT_EQ(code, StatusCode::Ok);
    }
}
