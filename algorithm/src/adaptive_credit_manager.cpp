#include "adaptive_credit_manager.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <vector>

namespace karma {

AdaptiveCreditManager::AdaptiveCreditManager(uint32_t total_blocks, 
                                           double alpha, 
                                           double beta, 
                                           double gamma,
                                           double history_weight)
    : total_blocks_(total_blocks),
      public_blocks_(static_cast<uint32_t>(alpha * total_blocks)),
      init_credits_(total_blocks_),
      alpha_(alpha),
      beta_(beta),
      gamma_(gamma),
      history_weight_(history_weight),
      history_window_(std::chrono::seconds(3600)) {}

void AdaptiveCreditManager::updateCredits(std::unordered_map<std::string, ResourceCredits>& user_credits) {
    // Remove unused current_time variable since we're not using it here
    for (auto& user_credit_pair : user_credits) {
        const std::string& user_id = user_credit_pair.first;
        auto& resource_credits = user_credit_pair.second.credits;

        for (auto& resource_credit_pair : resource_credits) {
            const std::string& resource_type = resource_credit_pair.first;
            uint64_t& current_credit = resource_credit_pair.second;

            // Update metrics and calculate adjustment
            updateUserMetrics(user_id, resource_type, current_credit);
            double adjustment = calculateCreditAdjustment(user_id, resource_type, current_credit);

            // Apply adjustment with bounds
            current_credit = std::max(getCreditFloor(),
                std::min(getCreditCeiling(),
                    static_cast<uint64_t>(current_credit + adjustment)));
        }
    }
}

void AdaptiveCreditManager::recordDonation(const std::string& user_id, 
                                         const std::string& resource_type, 
                                         uint32_t amount) {
    user_metrics_[user_id].total_donations[resource_type] += amount;
}

void AdaptiveCreditManager::recordBorrowing(const std::string& user_id, 
                                          const std::string& resource_type, 
                                          uint32_t amount) {
    user_metrics_[user_id].total_borrowings[resource_type] += amount;
}

double AdaptiveCreditManager::calculateCreditAdjustment(
    const std::string& user_id,
    const std::string& resource_type,
    uint64_t current_credit) {
    
    const auto& metrics = user_metrics_[user_id];
    double usage_score = calculateUsageScore(metrics, resource_type);
    
    // Calculate donation ratio for specific resource
    double donation_ratio = 0.5;  // Default balanced ratio
    if (metrics.total_donations.count(resource_type) > 0 && 
        metrics.total_borrowings.count(resource_type) > 0) {
        uint64_t total_donations = metrics.total_donations.at(resource_type);
        uint64_t total_borrowings = metrics.total_borrowings.at(resource_type);
        donation_ratio = static_cast<double>(total_donations) / 
            (total_donations + total_borrowings + 1);
    }
    
    // Calculate target credit based on usage and donation patterns
    double target_credit = gamma_ * usage_score * (1 + donation_ratio) * init_credits_;
    
    // Calculate adjustment with dampening factor
    return beta_ * (target_credit - current_credit);
}

void AdaptiveCreditManager::updateUserMetrics(
    const std::string& user_id,
    const std::string& resource_type,
    uint32_t current_demand) {
    
    auto& metrics = user_metrics_[user_id];
    auto current_time = std::chrono::system_clock::now();
    
    // Update usage history
    metrics.usage_history[resource_type].push_back({current_time, current_demand});
    
    // Clean up old history
    cleanupOldHistory(metrics, resource_type);
    
    // Update short-term and long-term usage
    if (!metrics.usage_history[resource_type].empty()) {
        metrics.short_term_usage[resource_type] = 
            static_cast<double>(metrics.usage_history[resource_type].back().second) / total_blocks_;
        
        double total_usage = 0;
        for (const auto& usage : metrics.usage_history[resource_type]) {
            total_usage += usage.second;
        }
        metrics.long_term_usage[resource_type] = total_usage / 
            (metrics.usage_history[resource_type].size() * total_blocks_);
    }
}

double AdaptiveCreditManager::calculateUsageScore(
    const UserMetrics& metrics,
    const std::string& resource_type) const {
    
    double short_term = 0.0;
    double long_term = 0.0;

    auto short_term_it = metrics.short_term_usage.find(resource_type);
    if (short_term_it != metrics.short_term_usage.end()) {
        short_term = short_term_it->second;
    }

    auto long_term_it = metrics.long_term_usage.find(resource_type);
    if (long_term_it != metrics.long_term_usage.end()) {
        long_term = long_term_it->second;
    }

    return history_weight_ * long_term + (1 - history_weight_) * short_term;
}

void AdaptiveCreditManager::cleanupOldHistory(
    UserMetrics& metrics,
    const std::string& resource_type) {
    
    auto current_time = std::chrono::system_clock::now();
    
    auto& history = metrics.usage_history[resource_type];
    while (!history.empty()) {
        if (current_time - history.front().first > history_window_) {
            history.pop_front();
        } else {
            break;
        }
    }
}

} // namespace karma