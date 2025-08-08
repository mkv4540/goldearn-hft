#include <gtest/gtest.h>
#include "../src/risk/risk_engine.hpp"

class RiskEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        risk_engine = std::make_unique<goldearn::risk::RiskEngine>();
    }
    
    std::unique_ptr<goldearn::risk::RiskEngine> risk_engine;
};

TEST_F(RiskEngineTest, BasicFunctionality) {
    EXPECT_NE(risk_engine, nullptr);
}