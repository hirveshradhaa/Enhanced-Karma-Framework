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
        std::cout << "   " << user << ":" << std::endl;
        for (const auto& resource : resource_types) {
            try {
                std::cout << "      " << std::setw(8) << resource << ": " 
                         << std::setw(12) << allocator.getCredits(user, resource) 
                         << std::endl;
            } catch (const std::exception& e) {
                std::cout << "      " << std::setw(8) << resource << ": ERROR" 
                         << std::endl;
            }
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

    std::cout << "Credit Balance Index: " << std::fixed << std::setprecision(3)
              << metrics.credit_balance_index << std::endl;

    std::cout << "Weighted Resource Utilization: " << std::fixed << std::setprecision(2)
              << metrics.weighted_resource_utilization * 100 << "%" << std::endl;
              
    std::cout << "----------------------------------------" << std::endl;
}

int main() {
    // Initialize system with multiple resources
    std::vector<karma::ResourceType> resources = {
        {"CPU", 2000, 0.2},     // CPU with 1000 blocks, 20% public
        {"Memory", 2000, 0.2},  // Memory with 4000 blocks, 10% public
        {"Network", 2000, 0.2}, // Network with 2000 blocks, 15% public
        {"Storage", 2000, 0.2}  // Storage with 8000 blocks, 5% public
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
