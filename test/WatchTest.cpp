#include <gtest/gtest.h>
#include <Etcd.hpp>
#include <array>
#include <future>

using namespace Etcd;

static constexpr const char* kAddress = "http://127.0.0.1:2379";

TEST(Watch, CanStartAndStop)
{
    auto logger = Etcd::Logger::CreateNull();
    std::shared_ptr<Client> client = Client::CreateV3(kAddress, logger);
    bool ok = client->StartWatch();
    ASSERT_TRUE(ok);
    client->StopWatch();
}

TEST(Watch, CanAddPrefixes)
{
    auto logger = Etcd::Logger::CreateNull();
    auto client = Client::CreateV3(kAddress, logger);

    bool ok = client->StartWatch();
    ASSERT_TRUE(ok);

    auto onKeyAdded = [](const std::string& key, const std::string& val) {};
    auto onKeyRemoved = [](const std::string& key) {};

    ok = client->AddWatchPrefix("my-prefix", onKeyAdded, onKeyRemoved);
    EXPECT_TRUE(ok);

    client->StopWatch();
}

TEST(Watch, CanRemovePrefixes)
{
    auto logger = Etcd::Logger::CreateNull();
    auto client = Client::CreateV3(kAddress, logger);

    bool ok = false;

    ok = client->StartWatch();
    ASSERT_TRUE(ok);

    auto onKeyAdded = [=](const std::string& key, const std::string& val) {};
    auto onKeyRemoved = [=](const std::string& key) {};

    ok = client->AddWatchPrefix("my-prefix", onKeyAdded, onKeyRemoved);
    EXPECT_TRUE(ok);

    ok = client->RemoveWatchPrefix("my-prefix");
    EXPECT_TRUE(ok);

    client->StopWatch();
}

TEST(Watch, MultiplePrefixesCanBeAdded)
{
    auto logger = Etcd::Logger::CreateNull();
    auto client = Client::CreateV3(kAddress, logger);

    bool ok = false;

    ok = client->StartWatch();
    ASSERT_TRUE(ok);

    auto onKeyAdded = [=](const std::string& key, const std::string& val) {};
    auto onKeyRemoved = [=](const std::string& key) {};

    ok = client->AddWatchPrefix("my-prefix1", onKeyAdded, onKeyRemoved);
    EXPECT_TRUE(ok);

    ok = client->AddWatchPrefix("my-prefix2", onKeyAdded, onKeyRemoved);
    EXPECT_TRUE(ok);

    ok = client->AddWatchPrefix("my-prefix3", onKeyAdded, onKeyRemoved);
    EXPECT_TRUE(ok);

    ok = client->AddWatchPrefix("my-prefix4", onKeyAdded, onKeyRemoved);
    EXPECT_TRUE(ok);

    ok = client->RemoveWatchPrefix("my-prefix1");
    EXPECT_TRUE(ok);

    ok = client->RemoveWatchPrefix("my-prefix2");
    EXPECT_TRUE(ok);

    ok = client->RemoveWatchPrefix("my-prefix3");
    EXPECT_TRUE(ok);

    ok = client->RemoveWatchPrefix("my-prefix4");
    EXPECT_TRUE(ok);

    client->StopWatch();
}

TEST(Watch, WachesKeysAddedAndRemoved)
{
    auto logger = Etcd::Logger::CreateNull();
    auto client = Client::CreateV3(kAddress, logger);

    bool ok = false;

    ok = client->StartWatch();
    ASSERT_TRUE(ok);

    size_t testIndex = 0;

    enum class Op {
        Add, Remove,
    };

    struct {
        std::string giveKey;
        std::string giveVal;
        bool wantMatch;
        Op op;
    } tests[] = {
        {"my-awesome-key", "value1", false, Op::Add},
        {"my-prefix1/super-key", "value2", true, Op::Add},
        {"my-prefix2/key", "value3", true, Op::Add},
        {"RandomKey", "val4", false, Op::Add},
        {"my-prefix3/123", "val5", false, Op::Add},
        {"RandomKey", "val4", false, Op::Remove},
        {"my-prefix20/1oj12oidj1o2idj", "val6", true, Op::Add},
    };

    auto onKeyAdded = [&](const std::string& key, const std::string& val) {
        auto test = tests[testIndex++];

        while (!test.wantMatch) {
            EXPECT_NE(test.giveKey, key);
            EXPECT_NE(test.giveVal, val);
            test = tests[testIndex++];
        }

        if (test.wantMatch) {
            EXPECT_EQ(test.op, Op::Add) << "should be add operation";
            EXPECT_EQ(test.giveKey, key);
            EXPECT_EQ(test.giveVal, val);
        }
    };

    auto onKeyRemoved = [&](const std::string& key) {
        auto test = tests[testIndex++];

        while (!test.wantMatch) {
            EXPECT_NE(test.giveKey, key);
            test = tests[testIndex++];
        }

        if (test.wantMatch) {
            EXPECT_EQ(test.op, Op::Remove) << "should be remove operation";
            EXPECT_EQ(test.giveKey, key);
        }
    };

    ok = client->AddWatchPrefix("my-prefix1/", onKeyAdded, onKeyRemoved);
    EXPECT_TRUE(ok);
    ok = client->AddWatchPrefix("my-prefix2/", onKeyAdded, onKeyRemoved);
    EXPECT_TRUE(ok);
    ok = client->AddWatchPrefix("my-prefix20/", onKeyAdded, onKeyRemoved);
    EXPECT_TRUE(ok);

    for (const auto& test : tests) {
        if (test.op == Op::Add) {
            auto code = client->Put(test.giveKey, test.giveVal);
            EXPECT_EQ(code, Etcd::StatusCode::Ok);
        } else {
            auto code = client->Delete(test.giveKey);
            EXPECT_EQ(code, Etcd::StatusCode::Ok);
        }
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    client->StopWatch();
}

// temporary
TEST(Watch, Connectivity)
{
    auto logger = Etcd::Logger::CreateStdout();
    auto client = Client::CreateV3(kAddress, logger);

    bool ok = false;

    ok = client->StartWatch();
    ASSERT_TRUE(ok);

    size_t testIndex = 0;

    enum class Op {
        Add, Remove,
    };

    struct {
        std::string giveKey;
        std::string giveVal;
        bool wantMatch;
        Op op;
    } tests[] = {
        {"my-awesome-key", "value1", false, Op::Add},
        {"my-prefix1/super-key", "value2", true, Op::Add},
        {"my-prefix2/key", "value3", true, Op::Add},
        {"RandomKey", "val4", false, Op::Add},
        {"my-prefix3/123", "val5", false, Op::Add},
        {"RandomKey", "val4", false, Op::Remove},
        {"my-prefix20/1oj12oidj1o2idj", "val6", true, Op::Add},
    };

    auto onKeyAdded = [&](const std::string& key, const std::string& val) {
        logger->Info("Key added: key = " + key + ", val = " + val);
    };

    auto onKeyRemoved = [&](const std::string& key) {
        logger->Info("Key removed: key = " + key);
    };

    ok = client->AddWatchPrefix("my-prefix1/", onKeyAdded, onKeyRemoved);
    EXPECT_TRUE(ok);
    ok = client->AddWatchPrefix("my-prefix2/", onKeyAdded, onKeyRemoved);
    EXPECT_TRUE(ok);
    ok = client->AddWatchPrefix("my-prefix20/", onKeyAdded, onKeyRemoved);
    EXPECT_TRUE(ok);

    std::this_thread::sleep_for(std::chrono::seconds(10));

#if 0
    for (const auto& test : tests) {
        if (test.op == Op::Add) {
            auto code = client->Put(test.giveKey, test.giveVal);
            EXPECT_EQ(code, Etcd::StatusCode::Ok);
        } else {
            auto code = client->Delete(test.giveKey);
            EXPECT_EQ(code, Etcd::StatusCode::Ok);
        }
    }
#endif
    logger->Info("Adding my-prefix50/");
    ok = client->AddWatchPrefix("my-prefix50/", onKeyAdded, onKeyRemoved);
    EXPECT_TRUE(ok);

    std::this_thread::sleep_for(std::chrono::seconds(10));

    ok = client->StartWatch();
    EXPECT_TRUE(ok);
    ok = client->AddWatchPrefix("my-prefix50/", onKeyAdded, onKeyRemoved);
    EXPECT_TRUE(ok);

    std::this_thread::sleep_for(std::chrono::seconds(10));

    client->StopWatch();
}
