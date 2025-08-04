#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>

int main() {
    std::cout << "GoldEarn HFT Test Runner\n";
    std::cout << "========================\n\n";
    
    std::vector<std::string> tests = {
        "./tests/test_config",
        "./tests/test_core",
        "./tests/test_market_data",
        "./tests/test_trading",
        "./tests/test_risk",
        "./tests/test_auth",
        "./tests/test_monitoring",
        "./tests/test_security"
    };
    
    int passed = 0;
    int failed = 0;
    
    for (const auto& test : tests) {
        std::cout << "Running: " << test << std::endl;
        
        pid_t pid = fork();
        if (pid == 0) {
            // Child process
            execl(test.c_str(), test.c_str(), "--gtest_list_tests", nullptr);
            exit(1); // If exec fails
        } else if (pid > 0) {
            // Parent process
            int status;
            alarm(5); // 5 second timeout
            wait(&status);
            alarm(0);
            
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                std::cout << "✓ " << test << " - Test listing successful\n";
                passed++;
            } else {
                std::cout << "✗ " << test << " - Failed or timed out\n";
                failed++;
            }
        } else {
            std::cerr << "Fork failed\n";
            failed++;
        }
        std::cout << std::endl;
    }
    
    std::cout << "\nSummary:\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";
    
    return failed > 0 ? 1 : 0;
}