#ifndef KARMA_BHEAP_H
#define KARMA_BHEAP_H
#include <utility>
#include <string>
#include <vector>
#include <queue>
#include <functional>

namespace karma {

typedef std::pair<std::string, int32_t> bheap_item;

struct bheap_comp { 
    constexpr bool operator()( 
        bheap_item const& a, 
        bheap_item const& b) 
        const noexcept 
    { 
        return a.second > b.second; 
    } 
};

class BroadcastHeap {
public:
    BroadcastHeap();
    
    void push(const std::string &key, int32_t value);

    bheap_item pop();

    int32_t min_val();

    std::size_t size();

    void add_to_all(int32_t val);

    bool empty() const;

private:
    // Internal heap
    std::priority_queue<bheap_item, std::vector<bheap_item>, bheap_comp> h_;
    
    int32_t base_val_;
};

}
#endif //KARMA_BHEAP_H