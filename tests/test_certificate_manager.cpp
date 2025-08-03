#include <gtest/gtest.h>
#include "../src/security/certificate_manager.hpp"

class CertificateManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        cert_manager = std::make_unique<goldearn::security::CertificateManager>();
    }
    
    std::unique_ptr<goldearn::security::CertificateManager> cert_manager;
};

TEST_F(CertificateManagerTest, BasicFunctionality) {
    EXPECT_NE(cert_manager, nullptr);
    EXPECT_FALSE(cert_manager->is_rotation_service_running());
}

TEST_F(CertificateManagerTest, RotationService) {
    cert_manager->start_rotation_service(std::chrono::hours(24));
    EXPECT_TRUE(cert_manager->is_rotation_service_running());
    cert_manager->stop_rotation_service();
    EXPECT_FALSE(cert_manager->is_rotation_service_running());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}