#ifndef ADAPTIVE_CREDIT_MANAGER_H
#define ADAPTIVE_CREDIT_MANAGER_H

#include <cstdint>
#include <unordered_map>
#include <string>
#include <vector>
#include <deque>
#include <chrono>
#include "resource_types.h"

namespace karma {

class AdaptiveCreditManager {
private:
    struct UserMetrics {
        std::unordered_map<std::string, double> short_term_usage;
        std::unordered_map<std::string, double> long_term_usage;
        std::unordered_map<std::string, 
            std::deque<std::pair<std::chrono::system_clock::time_point, uint32_t>>> usage_history;
        std::unordered_map<std::string, uint64_t> total_donations;
        std::unordered_map<std::string, uint64_t> total_borrowings;
    };

    uint32_t total_blocks_;
    uint32_t public_blocks_;
    uint64_t init_credits_;
    double alpha_;
    double beta_;
    double gamma_;
    double history_weight_;
    std::chrono::seconds history_window_;
    std::unordered_map<std::string, UserMetrics> user_metrics_;
    std::vector<std::string> resource_types_;

public:
    AdaptiveCreditManager() 
        : total_blocks_(0),
          public_blocks_(0),
          init_credits_(0),
          alpha_(0.0),
          beta_(0.1),
          gamma_(1.0),
          history_weight_(0.7),
          history_window_(std::chrono::seconds(3600)) {}

    AdaptiveCreditManager(uint32_t total_blocks, 
                         double alpha, 
                         double beta = 0.1, 
                         double gamma = 1.0,
                         double history_weight = 0.7);

    void updateCredits(std::unordered_map<std::string, ResourceCredits>& user_credits);
    void recordDonation(const std::string& user_id, const std::string& resource_type, uint32_t amount);
    void recordBorrowing(const std::string& user_id, const std::string& resource_type, uint32_t amount);
    uint64_t getCreditFloor() const { return init_credits_ / 10; }
    uint64_t getCreditCeiling() const { return init_credits_ * 10; }

private:
    double calculateCreditAdjustment(const std::string& user_id,
                                   const std::string& resource_type,
                                   uint64_t current_credit);
    void updateUserMetrics(const std::string& user_id, 
                          const std::string& resource_type,
                          uint32_t current_demand);
    double calculateUsageScore(const UserMetrics& metrics, 
                             const std::string& resource_type) const;
    void cleanupOldHistory(UserMetrics& metrics, const std::string& resource_type);
};

} // namespace karma

#endif // ADAPTIVE_CREDIT_MANAGER_H
