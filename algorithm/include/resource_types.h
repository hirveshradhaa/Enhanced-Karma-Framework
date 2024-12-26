#ifndef RESOURCE_TYPES_H
#define RESOURCE_TYPES_H

#include <string>
#include <unordered_map>
#include <vector>

namespace karma {

struct ResourceType {
    std::string name;
    uint32_t total_blocks;
    float alpha;  // Proportion of public blocks
};

struct ResourceCredits {
    std::unordered_map<std::string, uint64_t> credits; // Resource type -> credits
};

} // namespace karma

#endif // RESOURCE_TYPES_H
