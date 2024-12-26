// Karma all .h

#ifndef KARMA_ALLOCATOR_H
#define KARMA_ALLOCATOR_H

#include <vector>
#include <map>
#include <unordered_map>
#include <string>
#include <chrono>
#include "adaptive_credit_manager.h"

namespace karma {

struct ResourceType {
    std::string name;
    uint32_t total_blocks;
    float alpha;  // Proportion of public blocks
};

struct ResourceState {
    uint32_t total_blocks;
    uint32_t public_blocks;
    std::unordered_map<std::string, uint32_t> demands;
    std::unordered_map<std::string, uint32_t> allocations;
    std::unordered_map<std::string, int32_t> rate;
};

struct AllocationMetrics {
    std::unordered_map<std::string, uint32_t> total_allocated;
    std::unordered_map<std::string, uint32_t> total_demanded;
    std::unordered_map<std::string, double> resource_utilization;
    std::unordered_map<std::string, double> fairness_index;
    std::chrono::duration<double, std::nano> response_time{0.0};
};

class KarmaAllocator {
public:
    KarmaAllocator(const std::vector<ResourceType>& resources, uint64_t init_credits = 1000);
    virtual ~KarmaAllocator() = default;

    void addUser(std::string user_id);
    void updateDemand(std::string user_id, const std::string& resource_type, uint32_t demand);
    void allocate();
    uint32_t getAllocation(std::string user_id, const std::string& resource_type) const;
    uint32_t getDemand(std::string user_id, const std::string& resource_type) const;
    uint64_t getCredits(std::string user_id) const;
    AllocationMetrics getMetrics() const;

private:
    void borrowFromPoorestFast(const std::string& resource_type,
                              const std::vector<std::string>& donors,
                              uint32_t totalDemand);

    void giveToRichestFast(const std::string& resource_type,
                          const std::vector<std::string>& donors,
                          const std::vector<std::string>& borrowers);

    void updateAllocationMetrics();

    std::unordered_map<std::string, ResourceState> resources_;
    std::unordered_map<std::string, uint64_t> credits_;
    uint64_t init_credits_;
    uint32_t num_tenants_;

    AdaptiveCreditManager credit_manager_;
    AllocationMetrics current_metrics_;
};

} // namespace karma

#endif // KARMA_ALLOCATOR_H






// Karma all .cpp

#include "karma_allocator.h"
#include "bheap.h"
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <chrono>

namespace karma {

namespace {
    struct KarmaCandidate {
        std::string id;
        int64_t c;
        uint32_t x;
        KarmaCandidate(std::string id, int64_t c, uint32_t x) 
            : id(std::move(id)), c(c), x(x) {}
    };
}

KarmaAllocator::KarmaAllocator(const std::vector<ResourceType>& resources, uint64_t init_credits)
    : init_credits_(init_credits),
      num_tenants_(0),
      credit_manager_(resources.empty() ? 0 : resources[0].total_blocks,
                     resources.empty() ? 0.0 : resources[0].alpha) {
    
    if (resources.empty()) {
        throw std::invalid_argument("Resources vector cannot be empty");
    }

    // Initialize resource states
    for (const auto& resource : resources) {
        ResourceState state;
        state.total_blocks = resource.total_blocks;
        state.public_blocks = static_cast<uint32_t>(resource.alpha * resource.total_blocks);
        resources_.emplace(resource.name, std::move(state));
    }
}

void KarmaAllocator::addUser(std::string user_id) {
    if (!resources_.empty()) {
        auto first_resource = resources_.begin();
        if (first_resource->second.demands.find(user_id) == first_resource->second.demands.end()) {
            // Add user to all resources
            for (auto& resource_pair : resources_) {
                ResourceState& state = resource_pair.second;
                state.demands[user_id] = 0;
                state.allocations[user_id] = 0;
                state.rate[user_id] = 0;
            }
            credits_[user_id] = init_credits_;
            ++num_tenants_;
        }
    }
}

void KarmaAllocator::updateDemand(std::string user_id, const std::string& resource_type, uint32_t demand) {
    auto resource_it = resources_.find(resource_type);
    if (resource_it == resources_.end()) {
        throw std::runtime_error("Resource type not found: " + resource_type);
    }

    auto& state = resource_it->second;
    auto demand_it = state.demands.find(user_id);
    if (demand_it == state.demands.end()) {
        throw std::runtime_error("User not found: " + user_id);
    }
    demand_it->second = demand;
}

void KarmaAllocator::borrowFromPoorestFast(
    const std::string& resource_type,
    const std::vector<std::string>& donors,
    uint32_t total_demand) {
    
    auto& state = resources_.at(resource_type);
    const uint32_t fair_share = (state.total_blocks - state.public_blocks) / 
                               std::max(1U, num_tenants_);

    std::vector<KarmaCandidate> donor_list;
    for (const auto& donor : donors) {
        donor_list.emplace_back(donor, credits_[donor], fair_share - state.demands.at(donor));
    }

    if (state.public_blocks > 0) {
        donor_list.emplace_back("$public$", init_credits_, state.public_blocks);
    }

    std::sort(donor_list.begin(), donor_list.end(),
              [](const KarmaCandidate& a, const KarmaCandidate& b) {
                  return a.c < b.c;
              });

    uint32_t remaining_demand = total_demand;
    BroadcastHeap poorest_donors;

    for (const auto& donor : donor_list) {
        if (remaining_demand == 0) break;
        if (donor.x > 0) {
            poorest_donors.push(donor.id, donor.x);
        }
    }

    while (remaining_demand > 0 && !poorest_donors.empty()) {
        auto donor = poorest_donors.pop();
        uint32_t donation = std::min(static_cast<uint32_t>(donor.second), remaining_demand);
        
        if (donor.first != "$public$") {
            state.rate[donor.first] -= donation;
            credit_manager_.recordDonation(donor.first, donation);
        }
        
        remaining_demand -= donation;
    }
}

void KarmaAllocator::giveToRichestFast(
    const std::string& resource_type,
    const std::vector<std::string>& donors,
    const std::vector<std::string>& borrowers) {
    
    auto& state = resources_.at(resource_type);
    const uint32_t fair_share = (state.total_blocks - state.public_blocks) / 
                               std::max(1U, num_tenants_);

    uint32_t total_supply = state.public_blocks;
    for (const auto& donor : donors) {
        total_supply += fair_share - state.demands.at(donor);
    }

    std::vector<KarmaCandidate> borrower_list;
    for (const auto& borrower : borrowers) {
        uint32_t max_borrow = std::min(
            credits_[borrower],
            static_cast<uint64_t>(state.demands.at(borrower) - fair_share)
        );
        borrower_list.emplace_back(borrower, credits_[borrower], max_borrow);
    }

    std::sort(borrower_list.begin(), borrower_list.end(),
              [](const KarmaCandidate& a, const KarmaCandidate& b) {
                  return a.c > b.c;
              });

    uint32_t remaining_supply = total_supply;
    BroadcastHeap richest_borrowers;

    for (const auto& borrower : borrower_list) {
        if (borrower.x > 0) {
            richest_borrowers.push(borrower.id, borrower.x);
        }
    }

    while (remaining_supply > 0 && !richest_borrowers.empty()) {
        auto borrower = richest_borrowers.pop();
        uint32_t allocation = std::min(static_cast<uint32_t>(borrower.second), remaining_supply);
        
        state.allocations[borrower.first] += allocation;
        state.rate[borrower.first] += allocation;
        credit_manager_.recordBorrowing(borrower.first, allocation);
        
        remaining_supply -= allocation;
    }
}

void KarmaAllocator::allocate() {
    auto start_time = std::chrono::high_resolution_clock::now();

    // Calculate total demands per user across all resources
    std::unordered_map<std::string, uint32_t> total_demands;
    for (const auto& resource_pair : resources_) {
        const ResourceState& state = resource_pair.second;
        for (const auto& demand_pair : state.demands) {
            total_demands[demand_pair.first] += demand_pair.second;
        }
    }
    credit_manager_.updateCredits(total_demands, credits_);

    // Process each resource
    for (auto& resource_pair : resources_) {
        const std::string& resource_type = resource_pair.first;
        ResourceState& state = resource_pair.second;

        // Calculate fair share
        const uint32_t fair_share = (state.total_blocks - state.public_blocks) / 
                                  std::max(1U, num_tenants_);

        // Reset allocations
        for (auto& alloc_pair : state.allocations) {
            alloc_pair.second = 0;
        }

        std::vector<std::string> donors;
        std::vector<std::string> borrowers;
        uint32_t total_supply = state.public_blocks;
        uint32_t total_demand = 0;

        // Classify users
        for (const auto& demand_pair : state.demands) {
            const std::string& user_id = demand_pair.first;
            uint32_t demand = demand_pair.second;

            if (demand < fair_share) {
                donors.push_back(user_id);
                total_supply += fair_share - demand;
            } else if (demand > fair_share) {
                borrowers.push_back(user_id);
                total_demand += std::min(
                    static_cast<uint64_t>(demand - fair_share),
                    credits_[user_id]
                );
            }
            
            state.allocations[user_id] = std::min(demand, fair_share);
        }

        // Allocate resources based on supply and demand
        if (total_supply >= total_demand) {
            borrowFromPoorestFast(resource_type, donors, total_demand);
        } else {
            giveToRichestFast(resource_type, donors, borrowers);
        }

        // Apply allocation limits
        uint32_t max_allocation = state.total_blocks / std::max(1U, num_tenants_);
        for (auto& alloc_pair : state.allocations) {
            if (alloc_pair.second > max_allocation) {
                alloc_pair.second = max_allocation;
            }
        }
    }

    // Update timing metrics
    auto end_time = std::chrono::high_resolution_clock::now();
    current_metrics_.response_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end_time - start_time
    );

    updateAllocationMetrics();
}

void KarmaAllocator::updateAllocationMetrics() {
    for (const auto& resource_pair : resources_) {
        const std::string& resource_type = resource_pair.first;
        const ResourceState& state = resource_pair.second;

        // Initialize metrics
        current_metrics_.total_allocated[resource_type] = 0;
        current_metrics_.total_demanded[resource_type] = 0;
        current_metrics_.resource_utilization[resource_type] = 0.0;
        current_metrics_.fairness_index[resource_type] = 0.0;

        // Calculate totals
        for (const auto& alloc_pair : state.allocations) {
            current_metrics_.total_allocated[resource_type] += alloc_pair.second;
        }
        
        for (const auto& demand_pair : state.demands) {
            current_metrics_.total_demanded[resource_type] += demand_pair.second;
        }

        // Calculate utilization
        if (state.total_blocks > 0) {
            current_metrics_.resource_utilization[resource_type] = 
                static_cast<double>(current_metrics_.total_allocated[resource_type]) / 
                static_cast<double>(state.total_blocks);
        }

        // Calculate fairness
        if (num_tenants_ > 0) {
            double sum = 0.0;
            double sum_squared = 0.0;
            
            // Calculate normalized allocations
            for (const auto& alloc_pair : state.allocations) {
                const std::string& user_id = alloc_pair.first;
                double allocation = static_cast<double>(alloc_pair.second);
                double demand = static_cast<double>(std::max(1U, state.demands.at(user_id)));
                
                double normalized_allocation = allocation / demand;
                sum += normalized_allocation;
                sum_squared += normalized_allocation * normalized_allocation;
            }
            
            // Calculate fairness index only if we have valid data
            if (sum_squared > 0.0) {
                current_metrics_.fairness_index[resource_type] = 
                    (sum * sum) / (static_cast<double>(num_tenants_) * sum_squared);
            }
        }
    }
}

uint32_t KarmaAllocator::getAllocation(std::string user_id, const std::string& resource_type) const {
    const auto& state = resources_.at(resource_type);
    return state.allocations.at(user_id);
}

uint32_t KarmaAllocator::getDemand(std::string user_id, const std::string& resource_type) const {
    const auto& state = resources_.at(resource_type);
    return state.demands.at(user_id);
}

uint64_t KarmaAllocator::getCredits(std::string user_id) const {
    return credits_.at(user_id);
}

AllocationMetrics KarmaAllocator::getMetrics() const {
    return current_metrics_;
}

} // namespace karma





// ad .h

#ifndef ADAPTIVE_CREDIT_MANAGER_H
#define ADAPTIVE_CREDIT_MANAGER_H

#include <cstdint>
#include <unordered_map>
#include <string>
#include <deque>
#include <chrono>

namespace karma {

class AdaptiveCreditManager {
private:
    struct UserMetrics {
        double short_term_usage;
        double long_term_usage;
        std::deque<std::pair<std::chrono::system_clock::time_point, uint32_t>> usage_history;
        uint64_t total_donations;
        uint64_t total_borrowings;
    };

    uint32_t total_blocks_;
    uint32_t public_blocks_;
    uint64_t init_credits_;
    double alpha_;  // Factor for public blocks
    double beta_;   // Learning rate for credit adjustment
    double gamma_;  // Demand influence factor
    double history_weight_; // Weight for historical usage
    std::chrono::seconds history_window_; // Time window for usage history
    std::unordered_map<std::string, UserMetrics> user_metrics_;

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

    void updateCredits(const std::unordered_map<std::string, uint32_t>& demands,
                      std::unordered_map<std::string, uint64_t>& credits);

    void recordDonation(const std::string& user_id, uint32_t amount);
    void recordBorrowing(const std::string& user_id, uint32_t amount);

private:
    double calculateCreditAdjustment(const std::string& user_id,
                                   uint64_t current_credit, 
                                   uint32_t current_demand);
    void updateUserMetrics(const std::string& user_id, 
                          uint32_t current_demand);
    double calculateUsageScore(const UserMetrics& metrics) const;
    void cleanupOldHistory(UserMetrics& metrics);

private:
    double calculateCreditAdjustment(uint64_t current_credit, uint32_t demand);
};

} // namespace karma

#endif // ADAPTIVE_CREDIT_MANAGER_H



// ad.cpp 

#include "adaptive_credit_manager.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <numeric>

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
      history_window_(std::chrono::seconds(3600)) {} // 1 hour history window

void AdaptiveCreditManager::updateCredits(
    const std::unordered_map<std::string, uint32_t>& demands,
    std::unordered_map<std::string, uint64_t>& credits) {
    

    // Calculate system-wide metrics
    double total_demand = 0;
    for (const auto& pair : demands) {
        total_demand += pair.second;
    }

    // Update user metrics and adjust credits
    for (auto& credit_pair : credits) {
        const std::string& user_id = credit_pair.first;
        auto demand_it = demands.find(user_id);
        
        if (demand_it != demands.end()) {
            // Update usage metrics for this user
            updateUserMetrics(user_id, demand_it->second);
            
            // Calculate credit adjustment
            double adjustment = calculateCreditAdjustment(
                user_id, credit_pair.second, demand_it->second);

            // Apply adjustment with bounds
            credit_pair.second = std::max(
                static_cast<uint64_t>(0), // Ensure credits don't go negative
                static_cast<uint64_t>(credit_pair.second + adjustment)
            );
        }
    }

    // Normalize credits to maintain total sum
    uint64_t total_credits = 0;
    for (const auto& credit_pair : credits) {
        total_credits += credit_pair.second;
    }

    if (total_credits > 0) {
        double normalization_factor = static_cast<double>(total_blocks_) / total_credits;
        for (auto& credit_pair : credits) {
            credit_pair.second = static_cast<uint64_t>(
                credit_pair.second * normalization_factor);
        }
    }

}

void AdaptiveCreditManager::recordDonation(const std::string& user_id, uint32_t amount) {
    user_metrics_[user_id].total_donations += amount;
}

void AdaptiveCreditManager::recordBorrowing(const std::string& user_id, uint32_t amount) {
    user_metrics_[user_id].total_borrowings += amount;
}

double AdaptiveCreditManager::calculateCreditAdjustment(
    const std::string& user_id,
    uint64_t current_credit, 
    uint32_t current_demand) {
    
    const auto& metrics = user_metrics_[user_id];
    double usage_score = calculateUsageScore(metrics);
    
    // Normalize current values
    double normalized_demand = static_cast<double>(current_demand) / total_blocks_;
    double normalized_credit = static_cast<double>(current_credit) / total_blocks_;
    
    // Calculate donation ratio
    double donation_ratio = metrics.total_donations / 
        (static_cast<double>(metrics.total_donations + metrics.total_borrowings + 1));
    
    // Adjust the target credit calculation to be more balanced
    double target_credit = gamma_ * normalized_demand * usage_score * (1 + donation_ratio);
    
    // Fine-tune the adjustment factor to prevent rapid credit depletion
    double adjustment = beta_ * (target_credit - normalized_credit) * total_blocks_ * 0.25; // Further reduced impact
    
    return adjustment;
}

void AdaptiveCreditManager::updateUserMetrics(
    const std::string& user_id, 
    uint32_t current_demand) {
    
    auto& metrics = user_metrics_[user_id];
    auto current_time = std::chrono::system_clock::now();
    
    // Add new usage data
    metrics.usage_history.push_back({current_time, current_demand});
    
    // Clean up old history
    cleanupOldHistory(metrics);
    
    // Update short-term and long-term usage
    if (!metrics.usage_history.empty()) {
        metrics.short_term_usage = metrics.usage_history.back().second / 
            static_cast<double>(total_blocks_);
        
        double total_usage = 0;
        for (const auto& usage : metrics.usage_history) {
            total_usage += usage.second;
        }
        metrics.long_term_usage = total_usage / 
            (metrics.usage_history.size() * total_blocks_);
    }
}

double AdaptiveCreditManager::calculateUsageScore(const UserMetrics& metrics) const {
    return history_weight_ * metrics.long_term_usage + 
           (1 - history_weight_) * metrics.short_term_usage;
}

void AdaptiveCreditManager::cleanupOldHistory(UserMetrics& metrics) {
    auto current_time = std::chrono::system_clock::now();
    
    while (!metrics.usage_history.empty()) {
        auto& oldest = metrics.usage_history.front();
        if (current_time - oldest.first > history_window_) {
            metrics.usage_history.pop_front();
        } else {
            break;
        }
    }
}

} // namespace karma


// example.cpp

#include <iostream>
#include <string>
#include <iomanip>
#include <chrono>
#include "karma_allocator.h"

void printState(karma::KarmaAllocator& allocator, 
                const std::vector<std::string>& users,
                const std::vector<std::string>& resource_types) {
    std::cout << "\n=== Current System State ===" << std::endl;
    
    // Print demands and allocations for each resource
    for (const auto& resource : resource_types) {
        try {
            std::cout << "\nResource: " << resource << std::endl;
            std::cout << "Demands:" << std::endl;
            for (const auto& user : users) {
                try {
                    std::cout << std::setw(8) << user << ": " << std::setw(6) 
                             << allocator.getDemand(user, resource) << " blocks" << std::endl;
                } catch (const std::exception& e) {
                    std::cout << std::setw(8) << user << ": ERROR" << std::endl;
                }
            }
            
            std::cout << "\nAllocations:" << std::endl;
            uint32_t total_allocation = 0;
            for (const auto& user : users) {
                try {
                    uint32_t alloc = allocator.getAllocation(user, resource);
                    total_allocation += alloc;
                    std::cout << std::setw(8) << user << ": " << std::setw(6) 
                             << alloc << " blocks" << std::endl;
                } catch (const std::exception& e) {
                    std::cout << std::setw(8) << user << ": ERROR" << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cout << "Error accessing resource: " << resource << std::endl;
        }
    }
    
    // Print credits
    std::cout << "\nCredits:" << std::endl;
    for (const auto& user : users) {
        try {
            std::cout << std::setw(8) << user << ": " << std::setw(12) 
                     << allocator.getCredits(user) << std::endl;
        } catch (const std::exception& e) {
            std::cout << std::setw(8) << user << ": ERROR" << std::endl;
        }
    }
    
    // Print metrics for each resource
    auto metrics = allocator.getMetrics();
    std::cout << "\nMetrics per Resource:" << std::endl;
    for (const auto& resource : resource_types) {
        std::cout << "\nResource: " << resource << std::endl;
        
        try {
            // Access metrics safely with .find() instead of .at()
            auto total_alloc_it = metrics.total_allocated.find(resource);
            auto total_demand_it = metrics.total_demanded.find(resource);
            auto utilization_it = metrics.resource_utilization.find(resource);
            auto fairness_it = metrics.fairness_index.find(resource);

            if (total_alloc_it != metrics.total_allocated.end()) {
                std::cout << "Total Allocated: " << total_alloc_it->second << " blocks" << std::endl;
            }
            if (total_demand_it != metrics.total_demanded.end()) {
                std::cout << "Total Demanded: " << total_demand_it->second << " blocks" << std::endl;
            }
            if (utilization_it != metrics.resource_utilization.end()) {
                std::cout << "Resource Utilization: " << std::fixed << std::setprecision(2) 
                         << (utilization_it->second * 100) << "%" << std::endl;
            }
            if (fairness_it != metrics.fairness_index.end()) {
                std::cout << "Fairness Index: " << std::fixed << std::setprecision(3) 
                         << fairness_it->second << std::endl;
            }
        } catch (const std::exception& e) {
            std::cout << "Error accessing metrics for resource: " << resource << std::endl;
        }
    }
    
    std::cout << "\nResponse Time: " << std::fixed << std::setprecision(3)
              << std::chrono::duration_cast<std::chrono::microseconds>(metrics.response_time).count()
              << " µs" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
}

int main() {
    // Initialize system with multiple resources
    std::vector<karma::ResourceType> resources = {
        {"CPU", 1000, 0.2},     // CPU with 1000 blocks, 20% public
        {"Memory", 4000, 0.1},  // Memory with 4000 blocks, 10% public
        {"Network", 2000, 0.15}, // Network with 2000 blocks, 15% public
        {"Storage", 8000, 0.05}  // Storage with 8000 blocks, 5% public
    };
    
    karma::KarmaAllocator allocator(resources, 2000); // Initial credits: 2000
    
    std::vector<std::string> users = {"user1", "user2", "user3", "user4", "user5"};
    std::vector<std::string> resource_types = {"CPU", "Memory", "Network", "Storage"};
    
    // Add users
    for (const auto& user : users) {
        allocator.addUser(user);
    }

    std::cout << "\nTest Case 1: Initial State" << std::endl;
    allocator.allocate(); // Need to call allocate first to initialize metrics
    printState(allocator, users, resource_types);

    // Test Case 2: Equal Resource Demands
    std::cout << "\nTest Case 2: Equal Resource Demands" << std::endl;
    for (const auto& user : users) {
        allocator.updateDemand(user, "CPU", 200);
        allocator.updateDemand(user, "Memory", 800);
        allocator.updateDemand(user, "Network", 400);
        allocator.updateDemand(user, "Storage", 1600);
    }
    allocator.allocate();
    printState(allocator, users, resource_types);

    // Test Case 3: CPU-Intensive Workload
    std::cout << "\nTest Case 3: CPU-Intensive Workload" << std::endl;
    allocator.updateDemand("user1", "CPU", 800);
    allocator.updateDemand("user2", "CPU", 600);
    allocator.updateDemand("user3", "CPU", 400);
    // Keep other resources moderate
    for (const auto& user : {"user1", "user2", "user3"}) {
        allocator.updateDemand(user, "Memory", 400);
        allocator.updateDemand(user, "Network", 200);
        allocator.updateDemand(user, "Storage", 800);
    }
    allocator.allocate();
    printState(allocator, users, resource_types);

    // Test Case 4: Memory-Intensive Workload
    std::cout << "\nTest Case 4: Memory-Intensive Workload" << std::endl;
    allocator.updateDemand("user1", "Memory", 3000);
    allocator.updateDemand("user2", "Memory", 2500);
    allocator.updateDemand("user3", "Memory", 2000);
    allocator.allocate();
    printState(allocator, users, resource_types);

    // Test Case 5: Network and Storage Burst
    std::cout << "\nTest Case 5: Network and Storage Burst" << std::endl;
    allocator.updateDemand("user4", "Network", 1500);
    allocator.updateDemand("user4", "Storage", 6000);
    allocator.updateDemand("user5", "Network", 1200);
    allocator.updateDemand("user5", "Storage", 5000);
    allocator.allocate();
    printState(allocator, users, resource_types);

    // Test Case 6: Credit Depletion Test
    std::cout << "\nTest Case 6: Credit Depletion Test" << std::endl;
    // Make heavy demands repeatedly to test credit system
    for (int i = 0; i < 3; i++) {
        for (const auto& user : users) {
            allocator.updateDemand(user, "CPU", 900);
            allocator.updateDemand(user, "Memory", 3500);
            allocator.updateDemand(user, "Network", 1800);
            allocator.updateDemand(user, "Storage", 7000);
        }
        allocator.allocate();
        std::cout << "\nIteration " << (i + 1) << std::endl;
        printState(allocator, users, resource_types);
    }

    // Test Case 7: Resource Release Test
    std::cout << "\nTest Case 7: Resource Release Test" << std::endl;
    // Suddenly release most resources
    for (const auto& user : users) {
        allocator.updateDemand(user, "CPU", 50);
        allocator.updateDemand(user, "Memory", 200);
        allocator.updateDemand(user, "Network", 100);
        allocator.updateDemand(user, "Storage", 400);
    }
    allocator.allocate();
    printState(allocator, users, resource_types);

    // Test Case 8: Asymmetric Resource Demands
    std::cout << "\nTest Case 8: Asymmetric Resource Demands" << std::endl;
    allocator.updateDemand("user1", "CPU", 800);
    allocator.updateDemand("user1", "Memory", 200);
    allocator.updateDemand("user2", "Memory", 3000);
    allocator.updateDemand("user2", "Network", 300);
    allocator.updateDemand("user3", "Network", 1500);
    allocator.updateDemand("user3", "Storage", 6000);
    allocator.updateDemand("user4", "CPU", 600);
    allocator.updateDemand("user4", "Storage", 5000);
    allocator.updateDemand("user5", "Memory", 2500);
    allocator.updateDemand("user5", "Network", 1200);
    allocator.allocate();
    printState(allocator, users, resource_types);

    // Test Case 9: Public Resource Utilization
    std::cout << "\nTest Case 9: Public Resource Utilization" << std::endl;
    // Set demands just above fair share to test public resource distribution
    uint32_t cpu_fair = 160;    // (1000 * 0.8) / 5
    uint32_t mem_fair = 720;    // (4000 * 0.9) / 5
    uint32_t net_fair = 340;    // (2000 * 0.85) / 5
    uint32_t storage_fair = 1520; // (8000 * 0.95) / 5
    
    for (const auto& user : users) {
        allocator.updateDemand(user, "CPU", cpu_fair + 50);
        allocator.updateDemand(user, "Memory", mem_fair + 100);
        allocator.updateDemand(user, "Network", net_fair + 75);
        allocator.updateDemand(user, "Storage", storage_fair + 200);
    }
    allocator.allocate();
    printState(allocator, users, resource_types);

    // Test Case 10: Mixed Workload with Credit Recovery
    std::cout << "\nTest Case 10: Mixed Workload with Credit Recovery" << std::endl;
    // First let some users save credits
    for (int i = 0; i < 3; i++) {
        allocator.updateDemand(users[i], "CPU", 100);
        allocator.updateDemand(users[i], "Memory", 300);
        allocator.updateDemand(users[i], "Network", 150);
        allocator.updateDemand(users[i], "Storage", 600);
    }
    // While others spend credits
    for (int i = 3; i < 5; i++) {
        allocator.updateDemand(users[i], "CPU", 700);
        allocator.updateDemand(users[i], "Memory", 2800);
        allocator.updateDemand(users[i], "Network", 1400);
        allocator.updateDemand(users[i], "Storage", 5600);
    }
    allocator.allocate();
    printState(allocator, users, resource_types);

    return 0;
}