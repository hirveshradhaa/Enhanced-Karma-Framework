#ifndef KARMA_ALLOCATOR_H
#define KARMA_ALLOCATOR_H

#include <vector>
#include <map>
#include <unordered_map>
#include <string>
#include <chrono>
#include <algorithm>
#include "adaptive_credit_manager.h"
#include "resource_types.h"

namespace karma {

struct ResourceState {
    uint32_t total_blocks;
    uint32_t public_blocks;
    std::unordered_map<std::string, uint32_t> demands;
    std::unordered_map<std::string, uint32_t> allocations;
};

struct AllocationMetrics {
    std::unordered_map<std::string, uint32_t> total_allocated;
    std::unordered_map<std::string, uint32_t> total_demanded;
    std::unordered_map<std::string, double> resource_utilization;
    std::unordered_map<std::string, double> fairness_index;
    double credit_balance_index = 0.0;
    std::chrono::duration<double, std::nano> response_time{0.0};
    double weighted_resource_utilization = 0.0;
};

class KarmaAllocator {
public:
    KarmaAllocator(const std::vector<ResourceType>& resources, uint64_t init_credits = 1000);
    virtual ~KarmaAllocator() = default;

    void addUser(const std::string& user_id);
    void updateDemand(const std::string& user_id, const std::string& resource_type, uint32_t demand);
    void allocate();
    uint32_t getAllocation(const std::string& user_id, const std::string& resource_type) const;
    uint32_t getDemand(const std::string& user_id, const std::string& resource_type) const;
    uint64_t getCredits(const std::string& user_id, const std::string& resource_type) const;
    AllocationMetrics getMetrics() const;

private:
    void updateAllocationMetrics();

    std::unordered_map<std::string, ResourceState> resources_;
    std::unordered_map<std::string, ResourceCredits> user_credits_;
    uint64_t init_credits_;
    uint32_t num_tenants_;
    std::vector<std::string> resource_types_;

    AdaptiveCreditManager credit_manager_;
    AllocationMetrics current_metrics_;
};

} // namespace karma

#endif // KARMA_ALLOCATOR_H
