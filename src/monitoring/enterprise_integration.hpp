#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <nlohmann/json.hpp>

namespace goldearn::monitoring {

// SIEM (Security Information and Event Management) Integration
class SIEMIntegration {
public:
    virtual ~SIEMIntegration() = default;
    
    // Send security events to SIEM
    virtual bool send_security_event(const std::string& event_type,
                                   const std::string& severity,
                                   const std::string& source,
                                   const std::string& description,
                                   const nlohmann::json& details) = 0;
    
    // Send audit logs
    virtual bool send_audit_log(const std::string& user,
                              const std::string& action,
                              const std::string& resource,
                              const std::string& result,
                              const std::chrono::system_clock::time_point& timestamp) = 0;
    
    // Health check
    virtual bool is_healthy() = 0;
};

// Splunk SIEM Integration
class SplunkSIEMIntegration : public SIEMIntegration {
public:
    SplunkSIEMIntegration(const std::string& hec_url, const std::string& token,
                         const std::string& index = "goldearn");
    
    bool send_security_event(const std::string& event_type,
                           const std::string& severity,
                           const std::string& source,
                           const std::string& description,
                           const nlohmann::json& details) override;
    
    bool send_audit_log(const std::string& user,
                      const std::string& action,
                      const std::string& resource,
                      const std::string& result,
                      const std::chrono::system_clock::time_point& timestamp) override;
    
    bool is_healthy() override;
    
private:
    std::string make_http_request(const std::string& url, const std::string& data);
    
private:
    std::string hec_url_;
    std::string token_;
    std::string index_;
};

// IBM QRadar SIEM Integration
class QRadarSIEMIntegration : public SIEMIntegration {
public:
    QRadarSIEMIntegration(const std::string& console_url, const std::string& api_token);
    
    bool send_security_event(const std::string& event_type,
                           const std::string& severity,
                           const std::string& source,
                           const std::string& description,
                           const nlohmann::json& details) override;
    
    bool send_audit_log(const std::string& user,
                      const std::string& action,
                      const std::string& resource,
                      const std::string& result,
                      const std::chrono::system_clock::time_point& timestamp) override;
    
    bool is_healthy() override;
    
private:
    std::string console_url_;
    std::string api_token_;
};

// Generic Syslog SIEM Integration
class SyslogSIEMIntegration : public SIEMIntegration {
public:
    SyslogSIEMIntegration(const std::string& syslog_server, int port = 514,
                         const std::string& facility = "local0");
    
    bool send_security_event(const std::string& event_type,
                           const std::string& severity,
                           const std::string& source,
                           const std::string& description,
                           const nlohmann::json& details) override;
    
    bool send_audit_log(const std::string& user,
                      const std::string& action,
                      const std::string& resource,
                      const std::string& result,
                      const std::chrono::system_clock::time_point& timestamp) override;
    
    bool is_healthy() override;
    
private:
    int get_syslog_priority(const std::string& facility, const std::string& severity);
    
private:
    std::string syslog_server_;
    int port_;
    std::string facility_;
    int socket_fd_;
};

// Enterprise Monitoring Integration
class EnterpriseMonitoring {
public:
    EnterpriseMonitoring();
    ~EnterpriseMonitoring();
    
    // SIEM integration
    void add_siem_integration(std::shared_ptr<SIEMIntegration> siem);
    void remove_siem_integration(const std::string& name);
    
    // Event forwarding
    void forward_security_event(const std::string& event_type,
                              const std::string& severity,
                              const std::string& source,
                              const std::string& description,
                              const nlohmann::json& details = {});
    
    void forward_audit_event(const std::string& user,
                           const std::string& action,
                           const std::string& resource,
                           const std::string& result);
    
    // Compliance reporting
    void generate_sox_compliance_report(const std::string& output_path);
    void generate_pci_compliance_report(const std::string& output_path);
    void generate_audit_trail_report(const std::string& output_path,
                                   const std::chrono::system_clock::time_point& start_time,
                                   const std::chrono::system_clock::time_point& end_time);
    
    // Health monitoring
    bool check_siem_health();
    std::vector<std::string> get_unhealthy_integrations();
    
private:
    std::vector<std::shared_ptr<SIEMIntegration>> siem_integrations_;
    std::mutex integrations_mutex_;
    
    // Event queue for reliable delivery
    std::queue<nlohmann::json> event_queue_;
    std::mutex queue_mutex_;
    std::thread delivery_thread_;
    std::atomic<bool> running_{false};
    
    void delivery_thread_func();
    void process_event_queue();
};

// Hardware Security Module (HSM) Integration
class HSMIntegration {
public:
    virtual ~HSMIntegration() = default;
    
    // Key operations
    virtual std::string generate_key(const std::string& key_spec) = 0;
    virtual std::string encrypt(const std::string& key_id, const std::string& plaintext) = 0;
    virtual std::string decrypt(const std::string& key_id, const std::string& ciphertext) = 0;
    virtual std::string sign(const std::string& key_id, const std::string& data) = 0;
    virtual bool verify(const std::string& key_id, const std::string& data, const std::string& signature) = 0;
    
    // Key management
    virtual bool import_key(const std::string& key_id, const std::string& key_data) = 0;
    virtual bool delete_key(const std::string& key_id) = 0;
    virtual std::vector<std::string> list_keys() = 0;
    
    // HSM status
    virtual bool is_available() = 0;
    virtual std::string get_hsm_info() = 0;
};

// PKCS#11 HSM Integration
class PKCS11HSMIntegration : public HSMIntegration {
public:
    PKCS11HSMIntegration(const std::string& library_path, const std::string& slot_id,
                        const std::string& pin);
    ~PKCS11HSMIntegration();
    
    std::string generate_key(const std::string& key_spec) override;
    std::string encrypt(const std::string& key_id, const std::string& plaintext) override;
    std::string decrypt(const std::string& key_id, const std::string& ciphertext) override;
    std::string sign(const std::string& key_id, const std::string& data) override;
    bool verify(const std::string& key_id, const std::string& data, const std::string& signature) override;
    
    bool import_key(const std::string& key_id, const std::string& key_data) override;
    bool delete_key(const std::string& key_id) override;
    std::vector<std::string> list_keys() override;
    
    bool is_available() override;
    std::string get_hsm_info() override;
    
private:
    bool initialize_pkcs11();
    void cleanup_pkcs11();
    
private:
    std::string library_path_;
    std::string slot_id_;
    std::string pin_;
    
    void* pkcs11_library_;
    void* session_handle_;
    bool initialized_;
};

// Network Security Monitoring
class NetworkSecurityMonitor {
public:
    NetworkSecurityMonitor();
    ~NetworkSecurityMonitor();
    
    // Network monitoring
    void start_monitoring();
    void stop_monitoring();
    
    // Intrusion detection
    void enable_intrusion_detection(bool enabled = true);
    void add_blocked_ip(const std::string& ip_address, const std::string& reason);
    void remove_blocked_ip(const std::string& ip_address);
    bool is_ip_blocked(const std::string& ip_address);
    
    // DDoS protection
    void enable_ddos_protection(bool enabled = true);
    void set_rate_limits(int max_connections_per_ip, int max_requests_per_minute);
    
    // SSL/TLS monitoring
    void monitor_ssl_connections(bool enabled = true);
    void set_min_tls_version(const std::string& version);
    void add_trusted_certificate(const std::string& certificate_path);
    
    // Event callbacks
    void set_intrusion_callback(std::function<void(const std::string&, const std::string&)> callback);
    void set_ddos_callback(std::function<void(const std::string&, int)> callback);
    
private:
    void monitoring_thread_func();
    void check_network_connections();
    void analyze_traffic_patterns();
    
private:
    std::atomic<bool> monitoring_enabled_{false};
    std::thread monitoring_thread_;
    
    // Security settings
    bool intrusion_detection_enabled_{false};
    bool ddos_protection_enabled_{false};
    bool ssl_monitoring_enabled_{false};
    
    // Rate limiting
    int max_connections_per_ip_{100};
    int max_requests_per_minute_{1000};
    
    // Blocked IPs
    std::unordered_set<std::string> blocked_ips_;
    std::mutex blocked_ips_mutex_;
    
    // Callbacks
    std::function<void(const std::string&, const std::string&)> intrusion_callback_;
    std::function<void(const std::string&, int)> ddos_callback_;
};

} // namespace goldearn::monitoring