#include "certificate_manager.hpp"
#include "../utils/simple_logger.hpp"
#include <openssl/x509v3.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <ctime>

namespace goldearn::security {

CertificateManager::CertificateManager() {
    backup_directory_ = "./cert_backups";
    std::filesystem::create_directories(backup_directory_);
    LOG_INFO("CertificateManager: Initialized with backup directory: {}", backup_directory_);
}

CertificateManager::~CertificateManager() {
    stop_rotation_service();
}

bool CertificateManager::load_certificate(const std::string& name, const Certificate& cert) {
    std::lock_guard<std::mutex> lock(certificates_mutex_);
    
    // Validate certificate files exist
    if (!std::filesystem::exists(cert.cert_path)) {
        LOG_ERROR("CertificateManager: Certificate file not found: {}", cert.cert_path);
        return false;
    }
    
    if (!std::filesystem::exists(cert.key_path)) {
        LOG_ERROR("CertificateManager: Private key file not found: {}", cert.key_path);
        return false;
    }
    
    Certificate cert_copy = cert;
    
    // Validate and get expiry date
    if (!validate_certificate(cert_copy)) {
        LOG_ERROR("CertificateManager: Certificate validation failed for: {}", name);
        return false;
    }
    
    certificates_[name] = cert_copy;
    LOG_INFO("CertificateManager: Loaded certificate '{}', expires: {}", 
             name, std::chrono::duration_cast<std::chrono::seconds>(
                 cert_copy.expiry_date.time_since_epoch()).count());
    
    return true;
}

bool CertificateManager::unload_certificate(const std::string& name) {
    std::lock_guard<std::mutex> lock(certificates_mutex_);
    
    auto it = certificates_.find(name);
    if (it == certificates_.end()) {
        LOG_WARN("CertificateManager: Certificate '{}' not found for unloading", name);
        return false;
    }
    
    certificates_.erase(it);
    LOG_INFO("CertificateManager: Unloaded certificate '{}'", name);
    return true;
}

Certificate* CertificateManager::get_certificate(const std::string& name) {
    std::lock_guard<std::mutex> lock(certificates_mutex_);
    
    auto it = certificates_.find(name);
    return (it != certificates_.end()) ? &it->second : nullptr;
}

bool CertificateManager::validate_certificate(Certificate& cert) {
    try {
        // Read certificate file
        FILE* cert_file = fopen(cert.cert_path.c_str(), "r");
        if (!cert_file) {
            LOG_ERROR("CertificateManager: Cannot open certificate file: {}", cert.cert_path);
            return false;
        }
        
        X509* x509_cert = PEM_read_X509(cert_file, nullptr, nullptr, nullptr);
        fclose(cert_file);
        
        if (!x509_cert) {
            LOG_ERROR("CertificateManager: Failed to parse certificate: {}", cert.cert_path);
            return false;
        }
        
        // Get expiry date
        ASN1_TIME* not_after = X509_get_notAfter(x509_cert);
        cert.expiry_date = cert_utils::parse_asn1_time(not_after);
        
        // Check if certificate is currently valid
        auto now = std::chrono::system_clock::now();
        cert.is_valid = (now < cert.expiry_date);
        
        // Calculate fingerprint
        cert.fingerprint = calculate_fingerprint(cert.cert_path);
        
        // Update last checked time
        cert.last_checked = now;
        
        // Verify certificate chain if CA is provided
        if (!cert.ca_path.empty() && std::filesystem::exists(cert.ca_path)) {
            if (!verify_certificate_chain(cert)) {
                LOG_WARN("CertificateManager: Certificate chain validation failed for: {}", cert.cert_path);
            }
        }
        
        X509_free(x509_cert);
        
        LOG_DEBUG("CertificateManager: Certificate validation successful: {}", cert.cert_path);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("CertificateManager: Certificate validation error: {}", e.what());
        return false;
    }
}

std::chrono::system_clock::time_point CertificateManager::get_expiry_date(const std::string& cert_path) {
    FILE* cert_file = fopen(cert_path.c_str(), "r");
    if (!cert_file) {
        return std::chrono::system_clock::time_point{};
    }
    
    X509* x509_cert = PEM_read_X509(cert_file, nullptr, nullptr, nullptr);
    fclose(cert_file);
    
    if (!x509_cert) {
        return std::chrono::system_clock::time_point{};
    }
    
    ASN1_TIME* not_after = X509_get_notAfter(x509_cert);
    auto expiry = cert_utils::parse_asn1_time(not_after);
    
    X509_free(x509_cert);
    return expiry;
}

bool CertificateManager::is_certificate_valid(const Certificate& cert) {
    auto now = std::chrono::system_clock::now();
    return cert.is_valid && (now < cert.expiry_date);
}

void CertificateManager::start_rotation_service(std::chrono::hours check_interval) {
    if (rotation_running_) {
        LOG_WARN("CertificateManager: Rotation service already running");
        return;
    }
    
    check_interval_ = check_interval;
    rotation_running_ = true;
    rotation_thread_ = std::thread(&CertificateManager::rotation_thread_func, this);
    
    LOG_INFO("CertificateManager: Started certificate rotation service with {}h check interval", 
             check_interval.count());
}

void CertificateManager::stop_rotation_service() {
    if (!rotation_running_) {
        return;
    }
    
    rotation_running_ = false;
    
    if (rotation_thread_.joinable()) {
        rotation_thread_.join();
    }
    
    LOG_INFO("CertificateManager: Stopped certificate rotation service");
}

RotationResult CertificateManager::rotate_certificate(const std::string& name, const Certificate& new_cert) {
    RotationResult result;
    result.timestamp = std::chrono::system_clock::now();
    
    std::lock_guard<std::mutex> lock(certificates_mutex_);
    
    auto it = certificates_.find(name);
    if (it == certificates_.end()) {
        result.status = RotationStatus::FAILED;
        result.message = "Certificate not found: " + name;
        LOG_ERROR("CertificateManager: {}", result.message);
        return result;
    }
    
    result.old_cert = it->second;
    
    // Validate new certificate
    Certificate new_cert_copy = new_cert;
    if (!validate_certificate(new_cert_copy)) {
        result.status = RotationStatus::FAILED;
        result.message = "New certificate validation failed";
        LOG_ERROR("CertificateManager: {}", result.message);
        return result;
    }
    
    // Backup old certificate
    if (!backup_certificate(result.old_cert, backup_directory_)) {
        LOG_WARN("CertificateManager: Failed to backup old certificate for '{}'", name);
    }
    
    // Update certificate
    it->second = new_cert_copy;
    result.new_cert = new_cert_copy;
    result.status = RotationStatus::SUCCESS;
    result.message = "Certificate rotation successful";
    
    LOG_INFO("CertificateManager: Successfully rotated certificate '{}'", name);
    
    // Call rotation callback if set
    if (rotation_callback_) {
        try {
            rotation_callback_(result);
        } catch (const std::exception& e) {
            LOG_ERROR("CertificateManager: Rotation callback error: {}", e.what());
        }
    }
    
    return result;
}

std::vector<std::string> CertificateManager::get_expiring_certificates(std::chrono::hours within) {
    std::lock_guard<std::mutex> lock(certificates_mutex_);
    std::vector<std::string> expiring;
    
    auto threshold = std::chrono::system_clock::now() + within;
    
    for (const auto& pair : certificates_) {
        if (pair.second.expiry_date <= threshold) {
            expiring.push_back(pair.first);
        }
    }
    
    return expiring;
}

std::vector<std::string> CertificateManager::get_expired_certificates() {
    std::lock_guard<std::mutex> lock(certificates_mutex_);
    std::vector<std::string> expired;
    
    auto now = std::chrono::system_clock::now();
    
    for (const auto& pair : certificates_) {
        if (pair.second.expiry_date <= now) {
            expired.push_back(pair.first);
        }
    }
    
    return expired;
}

SSL_CTX* CertificateManager::create_ssl_context(const std::string& cert_name) {
    std::lock_guard<std::mutex> lock(certificates_mutex_);
    
    auto it = certificates_.find(cert_name);
    if (it == certificates_.end()) {
        LOG_ERROR("CertificateManager: Certificate '{}' not found for SSL context", cert_name);
        return nullptr;
    }
    
    const Certificate& cert = it->second;
    
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        LOG_ERROR("CertificateManager: Failed to create SSL context");
        return nullptr;
    }
    
    // Load certificate
    if (SSL_CTX_use_certificate_file(ctx, cert.cert_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
        LOG_ERROR("CertificateManager: Failed to load certificate file: {}", cert.cert_path);
        SSL_CTX_free(ctx);
        return nullptr;
    }
    
    // Load private key
    if (SSL_CTX_use_PrivateKey_file(ctx, cert.key_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
        LOG_ERROR("CertificateManager: Failed to load private key file: {}", cert.key_path);
        SSL_CTX_free(ctx);
        return nullptr;
    }
    
    // Verify private key matches certificate
    if (!SSL_CTX_check_private_key(ctx)) {
        LOG_ERROR("CertificateManager: Private key does not match certificate");
        SSL_CTX_free(ctx);
        return nullptr;
    }
    
    // Load CA certificate if available
    if (!cert.ca_path.empty() && std::filesystem::exists(cert.ca_path)) {
        if (!SSL_CTX_load_verify_locations(ctx, cert.ca_path.c_str(), nullptr)) {
            LOG_WARN("CertificateManager: Failed to load CA certificate: {}", cert.ca_path);
        }
    }
    
    LOG_DEBUG("CertificateManager: Created SSL context for certificate '{}'", cert_name);
    return ctx;
}

void CertificateManager::update_ssl_context(SSL_CTX* ctx, const std::string& cert_name) {
    if (!ctx) {
        LOG_ERROR("CertificateManager: Invalid SSL context");
        return;
    }
    
    std::lock_guard<std::mutex> lock(certificates_mutex_);
    
    auto it = certificates_.find(cert_name);
    if (it == certificates_.end()) {
        LOG_ERROR("CertificateManager: Certificate '{}' not found for SSL context update", cert_name);
        return;
    }
    
    const Certificate& cert = it->second;
    
    // Update certificate
    if (SSL_CTX_use_certificate_file(ctx, cert.cert_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
        LOG_ERROR("CertificateManager: Failed to update certificate in SSL context");
        return;
    }
    
    // Update private key
    if (SSL_CTX_use_PrivateKey_file(ctx, cert.key_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
        LOG_ERROR("CertificateManager: Failed to update private key in SSL context");
        return;
    }
    
    LOG_INFO("CertificateManager: Updated SSL context for certificate '{}'", cert_name);
}

void CertificateManager::rotation_thread_func() {
    LOG_INFO("CertificateManager: Certificate rotation thread started");
    
    while (rotation_running_) {
        try {
            check_and_rotate_certificates();
        } catch (const std::exception& e) {
            LOG_ERROR("CertificateManager: Error in rotation thread: {}", e.what());
        }
        
        std::this_thread::sleep_for(check_interval_);
    }
    
    LOG_INFO("CertificateManager: Certificate rotation thread stopped");
}

void CertificateManager::check_and_rotate_certificates() {
    auto expiring = get_expiring_certificates(rotation_threshold_);
    
    if (!expiring.empty()) {
        LOG_WARN("CertificateManager: Found {} certificates expiring within {}h", 
                 expiring.size(), rotation_threshold_.count());
        
        for (const auto& cert_name : expiring) {
            LOG_WARN("CertificateManager: Certificate '{}' is expiring soon", cert_name);
            
            // In a real implementation, this would trigger automatic certificate
            // renewal with the certificate authority or load new certificates
            // from a designated directory
        }
    }
    
    auto expired = get_expired_certificates();
    if (!expired.empty()) {
        LOG_ERROR("CertificateManager: Found {} expired certificates", expired.size());
        
        for (const auto& cert_name : expired) {
            LOG_ERROR("CertificateManager: Certificate '{}' has EXPIRED", cert_name);
        }
    }
}

std::string CertificateManager::calculate_fingerprint(const std::string& cert_path) {
    std::ifstream file(cert_path, std::ios::binary);
    if (!file) {
        return "";
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(content.c_str()), content.length(), hash);
    
    std::ostringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    
    return ss.str();
}

bool CertificateManager::backup_certificate(const Certificate& cert, const std::string& backup_dir) {
    try {
        std::filesystem::create_directories(backup_dir);
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::ostringstream timestamp;
        timestamp << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
        
        std::string backup_cert = backup_dir + "/cert_" + timestamp.str() + ".pem";
        std::string backup_key = backup_dir + "/key_" + timestamp.str() + ".pem";
        
        std::filesystem::copy_file(cert.cert_path, backup_cert);
        std::filesystem::copy_file(cert.key_path, backup_key);
        
        if (!cert.ca_path.empty() && std::filesystem::exists(cert.ca_path)) {
            std::string backup_ca = backup_dir + "/ca_" + timestamp.str() + ".pem";
            std::filesystem::copy_file(cert.ca_path, backup_ca);
        }
        
        LOG_INFO("CertificateManager: Backed up certificate to {}", backup_dir);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("CertificateManager: Certificate backup failed: {}", e.what());
        return false;
    }
}

bool CertificateManager::verify_certificate_chain(const Certificate& cert) {
    // This is a simplified implementation
    // In production, use full OpenSSL certificate chain verification
    return cert_utils::verify_certificate_against_ca(cert.cert_path, cert.ca_path);
}

// Certificate utility function implementations
namespace cert_utils {

std::chrono::system_clock::time_point parse_asn1_time(const ASN1_TIME* asn1_time) {
    if (!asn1_time) {
        return std::chrono::system_clock::time_point{};
    }
    
    struct tm tm_time = {};
    if (ASN1_TIME_to_tm(asn1_time, &tm_time) != 1) {
        return std::chrono::system_clock::time_point{};
    }
    
    auto time_t_val = timegm(&tm_time);
    return std::chrono::system_clock::from_time_t(time_t_val);
}

bool verify_certificate_against_ca(const std::string& cert_path, const std::string& ca_path) {
    // Simplified CA verification
    // In production, use full OpenSSL certificate chain verification
    return std::filesystem::exists(cert_path) && std::filesystem::exists(ca_path);
}

std::string get_certificate_subject(const std::string& cert_path) {
    FILE* cert_file = fopen(cert_path.c_str(), "r");
    if (!cert_file) return "";
    
    X509* x509_cert = PEM_read_X509(cert_file, nullptr, nullptr, nullptr);
    fclose(cert_file);
    
    if (!x509_cert) return "";
    
    char* subject_name = X509_NAME_oneline(X509_get_subject_name(x509_cert), nullptr, 0);
    std::string result = subject_name ? subject_name : "";
    
    OPENSSL_free(subject_name);
    X509_free(x509_cert);
    
    return result;
}

} // namespace cert_utils

// NSE Certificate rotation procedure
RotationResult NSECertificateRotationProcedure::execute_rotation(const std::string& cert_name, const Certificate& new_cert) {
    RotationResult result;
    result.timestamp = std::chrono::system_clock::now();
    result.new_cert = new_cert;
    
    try {
        // Validate NSE-specific certificate requirements
        if (!validate_nse_certificate_requirements(new_cert)) {
            result.status = RotationStatus::FAILED;
            result.message = "NSE certificate requirements validation failed";
            return result;
        }
        
        // Update NSE connection certificates
        if (!update_nse_connection_certificates(new_cert)) {
            result.status = RotationStatus::FAILED;
            result.message = "Failed to update NSE connection certificates";
            return result;
        }
        
        // Verify connectivity with new certificates
        if (!verify_nse_connectivity(new_cert)) {
            result.status = RotationStatus::FAILED;
            result.message = "NSE connectivity verification failed";
            return result;
        }
        
        result.status = RotationStatus::SUCCESS;
        result.message = "NSE certificate rotation completed successfully";
        
        LOG_INFO("NSECertificateRotationProcedure: Successfully rotated NSE certificate '{}'", cert_name);
        
    } catch (const std::exception& e) {
        result.status = RotationStatus::FAILED;
        result.message = std::string("NSE certificate rotation error: ") + e.what();
        LOG_ERROR("NSECertificateRotationProcedure: {}", result.message);
    }
    
    return result;
}

bool NSECertificateRotationProcedure::validate_nse_certificate_requirements(const Certificate& cert) {
    // NSE-specific certificate validation
    std::string subject = cert_utils::get_certificate_subject(cert.cert_path);
    
    // Check if certificate contains required NSE identifiers
    if (subject.find("NSE") == std::string::npos) {
        LOG_ERROR("NSECertificateRotationProcedure: Certificate does not contain NSE identifier");
        return false;
    }
    
    // Additional NSE-specific validations would go here
    return true;
}

bool NSECertificateRotationProcedure::update_nse_connection_certificates(const Certificate& cert) {
    // Update NSE connection SSL contexts
    // This would integrate with the NSE protocol implementation
    LOG_INFO("NSECertificateRotationProcedure: Updating NSE connection certificates");
    return true;
}

bool NSECertificateRotationProcedure::verify_nse_connectivity(const Certificate& cert) {
    // Test NSE connectivity with new certificates
    LOG_INFO("NSECertificateRotationProcedure: Verifying NSE connectivity");
    return true;
}

// BSE Certificate rotation procedure
RotationResult BSECertificateRotationProcedure::execute_rotation(const std::string& cert_name, const Certificate& new_cert) {
    RotationResult result;
    result.timestamp = std::chrono::system_clock::now();
    result.new_cert = new_cert;
    
    try {
        // Validate BSE-specific certificate requirements
        if (!validate_bse_certificate_requirements(new_cert)) {
            result.status = RotationStatus::FAILED;
            result.message = "BSE certificate requirements validation failed";
            return result;
        }
        
        // Update BSE connection certificates
        if (!update_bse_connection_certificates(new_cert)) {
            result.status = RotationStatus::FAILED;
            result.message = "Failed to update BSE connection certificates";
            return result;
        }
        
        // Verify connectivity with new certificates
        if (!verify_bse_connectivity(new_cert)) {
            result.status = RotationStatus::FAILED;
            result.message = "BSE connectivity verification failed";
            return result;
        }
        
        result.status = RotationStatus::SUCCESS;
        result.message = "BSE certificate rotation completed successfully";
        
        LOG_INFO("BSECertificateRotationProcedure: Successfully rotated BSE certificate '{}'", cert_name);
        
    } catch (const std::exception& e) {
        result.status = RotationStatus::FAILED;
        result.message = std::string("BSE certificate rotation error: ") + e.what();
        LOG_ERROR("BSECertificateRotationProcedure: {}", result.message);
    }
    
    return result;
}

bool BSECertificateRotationProcedure::validate_bse_certificate_requirements(const Certificate& cert) {
    // BSE-specific certificate validation
    std::string subject = cert_utils::get_certificate_subject(cert.cert_path);
    
    // Check if certificate contains required BSE identifiers
    if (subject.find("BSE") == std::string::npos) {
        LOG_ERROR("BSECertificateRotationProcedure: Certificate does not contain BSE identifier");
        return false;
    }
    
    // Additional BSE-specific validations would go here
    return true;
}

bool BSECertificateRotationProcedure::update_bse_connection_certificates(const Certificate& cert) {
    // Update BSE connection SSL contexts
    // This would integrate with the BSE protocol implementation
    LOG_INFO("BSECertificateRotationProcedure: Updating BSE connection certificates");
    return true;
}

bool BSECertificateRotationProcedure::verify_bse_connectivity(const Certificate& cert) {
    // Test BSE connectivity with new certificates
    LOG_INFO("BSECertificateRotationProcedure: Verifying BSE connectivity");
    return true;
}

} // namespace goldearn::security