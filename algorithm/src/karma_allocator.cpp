#include "karma_allocator.h"
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <chrono>
#include <numeric>
#include <vector>

namespace karma {

KarmaAllocator::KarmaAllocator(const std::vector<ResourceType>& resources, uint64_t init_credits)
    : init_credits_(init_credits),
      num_tenants_(0),
      credit_manager_(resources.empty() ? 0 : resources[0].total_blocks,
                     resources.empty() ? 0.0 : resources[0].alpha) {

    if (resources.empty()) {
        throw std::invalid_argument("Resources vector cannot be empty");
    }

    // Initialize resource states and store resource types
    for (const auto& resource : resources) {
        ResourceState state;
        state.total_blocks = resource.total_blocks;
        state.public_blocks = static_cast<uint32_t>(resource.alpha * resource.total_blocks);
        resources_.emplace(resource.name, std::move(state));
        resource_types_.push_back(resource.name);
    }
}

void KarmaAllocator::addUser(const std::string& user_id) {
    if (!resources_.empty()) {
        auto first_resource = resources_.begin();
        if (first_resource->second.demands.find(user_id) == first_resource->second.demands.end()) {
            // Add user to all resources
            ResourceCredits user_resource_credits;
            for (auto& resource_pair : resources_) {
                const std::string& resource_type = resource_pair.first;
                ResourceState& state = resource_pair.second;
                state.demands[user_id] = 0;
                state.allocations[user_id] = 0;
                user_resource_credits.credits[resource_type] = init_credits_;
            }
            user_credits_[user_id] = std::move(user_resource_credits);
            ++num_tenants_;
        }
    }
}

void KarmaAllocator::updateDemand(const std::string& user_id, const std::string& resource_type, uint32_t demand) {
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

void KarmaAllocator::allocate() {
    auto start_time = std::chrono::high_resolution_clock::now();

    // Update credits based on previous allocations
    credit_manager_.updateCredits(user_credits_);

    // For each resource
    for (auto& resource_pair : resources_) {
        const std::string& resource_type = resource_pair.first;
        ResourceState& state = resource_pair.second;

        // Calculate fair share
        const uint32_t fair_share = (state.total_blocks - state.public_blocks) / 
                                   std::max(1U, num_tenants_);

        // Prepare variables for allocation
        std::vector<std::string> users_with_extra_demand;
        uint64_t total_extra_demand = 0;
        uint64_t total_credits = 0;
        uint32_t total_unused_fair_share = 0;

        // First, allocate fair share or less
        for (auto& alloc_pair : state.allocations) {
            const std::string& user_id = alloc_pair.first;
            uint32_t demand = state.demands[user_id];
            uint32_t allocation = std::min(demand, fair_share);
            alloc_pair.second = allocation;

            // Compute extra demand
            uint32_t extra_demand = (demand > fair_share) ? (demand - fair_share) : 0;

            if (extra_demand > 0) {
                users_with_extra_demand.push_back(user_id);
                total_extra_demand += extra_demand;
                total_credits += user_credits_[user_id].credits[resource_type];
            } else if (allocation < fair_share) {
                total_unused_fair_share += (fair_share - allocation);
            }
        }

        // Distribute unused fair share based on credits
        if (total_unused_fair_share > 0 && !users_with_extra_demand.empty()) {
            for (const auto& user_id : users_with_extra_demand) {
                uint64_t user_credits = user_credits_[user_id].credits[resource_type];
                double credit_ratio = static_cast<double>(user_credits) / total_credits;
                
                uint32_t extra_allocation = static_cast<uint32_t>(credit_ratio * total_unused_fair_share);
                uint32_t demand = state.demands[user_id];
                uint32_t current_allocation = state.allocations[user_id];
                uint32_t max_possible_allocation = std::min(demand, state.total_blocks);
                uint32_t new_allocation = std::min(current_allocation + extra_allocation, 
                                                 max_possible_allocation);

                // Update allocation
                uint32_t actual_extra = new_allocation - current_allocation;
                state.allocations[user_id] = new_allocation;

                // Update credits
                if (actual_extra > 0) {
                    uint64_t& user_resource_credits = user_credits_[user_id].credits[resource_type];
                    user_resource_credits = (user_resource_credits > actual_extra) ? 
                                          (user_resource_credits - actual_extra) : 0;
                    credit_manager_.recordBorrowing(user_id, resource_type, actual_extra);
                }
            }
        }

        // Distribute public blocks based on credits
        uint32_t remaining_public_blocks = state.public_blocks;
        if (remaining_public_blocks > 0 && !users_with_extra_demand.empty()) {
            // Recalculate total credits
            total_credits = 0;
            for (const auto& user_id : users_with_extra_demand) {
                total_credits += user_credits_[user_id].credits[resource_type];
            }

            if (total_credits > 0) {
                for (const auto& user_id : users_with_extra_demand) {
                    uint64_t user_credits = user_credits_[user_id].credits[resource_type];
                    double credit_ratio = static_cast<double>(user_credits) / total_credits;

                    uint32_t extra_allocation = static_cast<uint32_t>(credit_ratio * remaining_public_blocks);
                    uint32_t demand = state.demands[user_id];
                    uint32_t current_allocation = state.allocations[user_id];
                    uint32_t max_possible_allocation = std::min(demand, state.total_blocks);
                    uint32_t new_allocation = std::min(current_allocation + extra_allocation, 
                                                     max_possible_allocation);

                    // Update allocation
                    uint32_t actual_extra = new_allocation - current_allocation;
                    state.allocations[user_id] = new_allocation;

                    // Update credits
                    if (actual_extra > 0) {
                        uint64_t& user_resource_credits = user_credits_[user_id].credits[resource_type];
                        user_resource_credits = (user_resource_credits > actual_extra) ? 
                                              (user_resource_credits - actual_extra) : 0;
                        credit_manager_.recordBorrowing(user_id, resource_type, actual_extra);
                        remaining_public_blocks -= actual_extra;
                    }

                                        if (remaining_public_blocks == 0) break;
                }
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

uint32_t KarmaAllocator::getAllocation(const std::string& user_id, const std::string& resource_type) const {
    auto resource_it = resources_.find(resource_type);
    if (resource_it == resources_.end()) {
        throw std::runtime_error("Resource type not found: " + resource_type);
    }

    const auto& state = resource_it->second;
    auto alloc_it = state.allocations.find(user_id);
    if (alloc_it == state.allocations.end()) {
        throw std::runtime_error("User not found: " + user_id);
    }

    return alloc_it->second;
}

uint32_t KarmaAllocator::getDemand(const std::string& user_id, const std::string& resource_type) const {
    auto resource_it = resources_.find(resource_type);
    if (resource_it == resources_.end()) {
        throw std::runtime_error("Resource type not found: " + resource_type);
    }

    const auto& state = resource_it->second;
    auto demand_it = state.demands.find(user_id);
    if (demand_it == state.demands.end()) {
        throw std::runtime_error("User not found: " + user_id);
    }

    return demand_it->second;
}

uint64_t KarmaAllocator::getCredits(const std::string& user_id, const std::string& resource_type) const {
    auto user_it = user_credits_.find(user_id);
    if (user_it == user_credits_.end()) {
        throw std::runtime_error("User not found: " + user_id);
    }
    
    auto credit_it = user_it->second.credits.find(resource_type);
    if (credit_it == user_it->second.credits.end()) {
        throw std::runtime_error("Resource type not found: " + resource_type);
    }
    
    return credit_it->second;
}

AllocationMetrics KarmaAllocator::getMetrics() const {
    return current_metrics_;
}

void KarmaAllocator::updateAllocationMetrics() {
    // Reset metrics
    current_metrics_.total_allocated.clear();
    current_metrics_.total_demanded.clear();
    current_metrics_.resource_utilization.clear();
    current_metrics_.fairness_index.clear();
    current_metrics_.credit_balance_index = 0.0;
    current_metrics_.weighted_resource_utilization = 0.0;

    double total_resource_weight = 0.0;
    
    // Calculate metrics for each resource
    for (const auto& resource_pair : resources_) {
        const std::string& resource_type = resource_pair.first;
        const ResourceState& state = resource_pair.second;

        uint32_t total_allocated = 0;
        uint32_t total_demanded = 0;
        double sum_allocations = 0.0;
        double sum_squared_allocations = 0.0;
        
        // Calculate totals and sums for fairness index
        for (const auto& alloc_pair : state.allocations) {
            const std::string& user_id = alloc_pair.first;
            uint32_t allocation = alloc_pair.second;
            uint32_t demand = state.demands.at(user_id);
            
            total_allocated += allocation;
            total_demanded += demand;
            sum_allocations += allocation;
            sum_squared_allocations += allocation * allocation;
        }

        // Store total allocation and demand
        current_metrics_.total_allocated[resource_type] = total_allocated;
        current_metrics_.total_demanded[resource_type] = total_demanded;

        // Calculate resource utilization
        double utilization = (state.total_blocks > 0) ? 
            static_cast<double>(total_allocated) / state.total_blocks : 0.0;
        current_metrics_.resource_utilization[resource_type] = utilization;

        // Calculate Jain's fairness index
        size_t n = state.allocations.size();
        if (n > 0 && sum_allocations > 0) {
            double fairness = (sum_allocations * sum_allocations) / 
                            (n * sum_squared_allocations);
            current_metrics_.fairness_index[resource_type] = fairness;
        } else {
            current_metrics_.fairness_index[resource_type] = 1.0;
        }

        // Add to weighted resource utilization
        double resource_weight = static_cast<double>(state.total_blocks);
        current_metrics_.weighted_resource_utilization += utilization * resource_weight;
        total_resource_weight += resource_weight;
    }

    // Finalize weighted resource utilization
    if (total_resource_weight > 0) {
        current_metrics_.weighted_resource_utilization /= total_resource_weight;
    }

    // Calculate credit balance index
    double sum_credits = 0.0;
    double sum_squared_credits = 0.0;
    size_t num_users = user_credits_.size();

    for (const auto& user_credit_pair : user_credits_) {
        double total_user_credits = 0.0;
        for (const auto& resource_credit : user_credit_pair.second.credits) {
            total_user_credits += resource_credit.second;
        }
        sum_credits += total_user_credits;
        sum_squared_credits += total_user_credits * total_user_credits;
    }

    if (num_users > 0 && sum_credits > 0) {
        current_metrics_.credit_balance_index = 
            (sum_credits * sum_credits) / (num_users * sum_squared_credits);
    } else {
        current_metrics_.credit_balance_index = 1.0;
    }
}

} // namespace karma
