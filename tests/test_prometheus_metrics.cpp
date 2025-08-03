#include <gtest/gtest.h>
#include "../src/monitoring/prometheus_metrics.hpp"

class PrometheusMetricsTest : public ::testing::Test {
protected:
    void SetUp() override {
        metrics_collector = std::make_unique<goldearn::monitoring::HFTMetricsCollector>();
    }
    
    std::unique_ptr<goldearn::monitoring::HFTMetricsCollector> metrics_collector;
};

TEST_F(PrometheusMetricsTest, BasicFunctionality) {
    EXPECT_NE(metrics_collector, nullptr);
}

TEST_F(PrometheusMetricsTest, MetricsRecording) {
    metrics_collector->record_order_latency(50.0);
    metrics_collector->record_order_placed();
    metrics_collector->record_market_data_message();
    
    std::string snapshot = metrics_collector->get_metrics_snapshot();
    EXPECT_FALSE(snapshot.empty());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}