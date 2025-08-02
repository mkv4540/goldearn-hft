#include <gtest/gtest.h>
#include "../src/monitoring/health_check.hpp"

class HealthCheckTest : public ::testing::Test {
protected:
    void SetUp() override {
        health_server = std::make_unique<goldearn::monitoring::HealthCheckServer>(8081);
    }
    
    std::unique_ptr<goldearn::monitoring::HealthCheckServer> health_server;
};

TEST_F(HealthCheckTest, BasicFunctionality) {
    EXPECT_NE(health_server, nullptr);
    EXPECT_FALSE(health_server->is_running());
}

TEST_F(HealthCheckTest, StartStop) {
    EXPECT_TRUE(health_server->start());
    EXPECT_TRUE(health_server->is_running());
    health_server->stop();
    EXPECT_FALSE(health_server->is_running());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}