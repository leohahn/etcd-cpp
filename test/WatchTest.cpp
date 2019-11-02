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

TEST(Watch, CanListenForPrefixes)
{
    auto logger = Etcd::Logger::CreateStdout();
    auto client = Client::CreateV3(kAddress, logger);

    bool ok = client->StartWatch();
    ASSERT_TRUE(ok);

    size_t testIndex = 0;

    struct {
        std::string giveKey;
        std::string giveVal;
        bool wantMatch;
    } tests[] = {
        {"my-awesome-key", "value1", false},
        {"my-prefix/super-key", "value2", true},
        {"my-prefix/key", "value3", true},
        {"RandomKey", "val1", false},
    };

    auto onKeyAdded = [=](const std::string& key, const std::string& val) {
        logger->Info("Key added: " + key);
        logger->Info("Val added: " + val);
    };

    auto onKeyRemoved = [=](const std::string& key) {
        logger->Info("Key removed: " + key);
    };

    auto done = std::make_shared<std::promise<void>>();
    std::future<void> doneFut = done->get_future();

    client->AddWatchPrefix("my-prefix", std::move(onKeyAdded), std::move(onKeyRemoved), [=]() {
        logger->Info("Watch was created!");
        done->set_value();
    });

    // Should not be a timeout
    ASSERT_NE(doneFut.wait_for(std::chrono::milliseconds(60)), std::future_status::timeout);

    client->StopWatch();
}

