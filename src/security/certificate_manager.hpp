#pragma once

#include <string>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>
#include <unordered_map>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

namespace goldearn::security {

struct Certificate {
    std::string cert_path;
    std::string key_path;
    std::string ca_path;
    std::chrono::system_clock::time_point expiry_date;
    std::chrono::system_clock::time_point last_checked;
    bool is_valid;
    std::string fingerprint;
};

enum class RotationStatus {
    SUCCESS,
    FAILED,
    IN_PROGRESS,
    NOT_NEEDED
};

struct RotationResult {
    RotationStatus status;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    Certificate old_cert;
    Certificate new_cert;
};

// Certificate rotation callback
using CertificateRotationCallback = std::function<void(const RotationResult&)>;

class CertificateManager {
public:
    CertificateManager();
    ~CertificateManager();
    
    // Certificate management
    bool load_certificate(const std::string& name, const Certificate& cert);
    bool unload_certificate(const std::string& name);
    Certificate* get_certificate(const std::string& name);
    
    // Certificate validation
    bool validate_certificate(Certificate& cert);
    std::chrono::system_clock::time_point get_expiry_date(const std::string& cert_path);
    bool is_certificate_valid(const Certificate& cert);
    
    // Automatic rotation
    void start_rotation_service(std::chrono::hours check_interval = std::chrono::hours(24));
    void stop_rotation_service();
    bool is_rotation_service_running() const { return rotation_running_; }
    
    // Manual rotation
    RotationResult rotate_certificate(const std::string& name, const Certificate& new_cert);
    
    // Configuration
    void set_rotation_threshold(std::chrono::hours threshold) { rotation_threshold_ = threshold; }
    void set_callback(CertificateRotationCallback callback) { rotation_callback_ = callback; }
    
    // Certificate monitoring
    std::vector<std::string> get_expiring_certificates(std::chrono::hours within = std::chrono::hours(24 * 30)); // 30 days
    std::vector<std::string> get_expired_certificates();
    
    // SSL context management
    SSL_CTX* create_ssl_context(const std::string& cert_name);
    void update_ssl_context(SSL_CTX* ctx, const std::string& cert_name);
    
private:
    void rotation_thread_func();
    void check_and_rotate_certificates();
    std::string calculate_fingerprint(const std::string& cert_path);
    bool backup_certificate(const Certificate& cert, const std::string& backup_dir);
    bool verify_certificate_chain(const Certificate& cert);
    
private:
    std::unordered_map<std::string, Certificate> certificates_;
    mutable std::mutex certificates_mutex_;
    
    std::atomic<bool> rotation_running_{false};
    std::thread rotation_thread_;
    std::chrono::hours check_interval_{24};
    std::chrono::hours rotation_threshold_{24 * 7}; // 7 days before expiry
    
    CertificateRotationCallback rotation_callback_;
    
    std::string backup_directory_;
};

// Certificate utility functions
namespace cert_utils {
    std::string get_certificate_subject(const std::string& cert_path);
    std::string get_certificate_issuer(const std::string& cert_path);
    std::vector<std::string> get_certificate_san(const std::string& cert_path);
    bool verify_certificate_against_ca(const std::string& cert_path, const std::string& ca_path);
    std::chrono::system_clock::time_point parse_asn1_time(const ASN1_TIME* asn1_time);
    bool create_certificate_backup(const Certificate& cert, const std::string& backup_path);
}

// Certificate rotation procedures
class CertificateRotationProcedure {
public:
    virtual ~CertificateRotationProcedure() = default;
    virtual RotationResult execute_rotation(const std::string& cert_name, const Certificate& new_cert) = 0;
    virtual std::string get_procedure_name() const = 0;
};

// NSE certificate rotation procedure
class NSECertificateRotationProcedure : public CertificateRotationProcedure {
public:
    RotationResult execute_rotation(const std::string& cert_name, const Certificate& new_cert) override;
    std::string get_procedure_name() const override { return "NSE_Certificate_Rotation"; }
    
private:
    bool validate_nse_certificate_requirements(const Certificate& cert);
    bool update_nse_connection_certificates(const Certificate& cert);
    bool verify_nse_connectivity(const Certificate& cert);
};

// BSE certificate rotation procedure
class BSECertificateRotationProcedure : public CertificateRotationProcedure {
public:
    RotationResult execute_rotation(const std::string& cert_name, const Certificate& new_cert) override;
    std::string get_procedure_name() const override { return "BSE_Certificate_Rotation"; }
    
private:
    bool validate_bse_certificate_requirements(const Certificate& cert);
    bool update_bse_connection_certificates(const Certificate& cert);
    bool verify_bse_connectivity(const Certificate& cert);
};

} // namespace goldearn::security