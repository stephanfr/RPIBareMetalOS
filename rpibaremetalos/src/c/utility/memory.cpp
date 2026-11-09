#include <memory>
#include "os_memory_config.h"
#include "asm_globals.h"
#include "heaps.h"
#include "synchronization.h"
#include "__memory_resource/lockfree_composite_single_arena_resource.h"
#include "__memory_resource/lockfree_static_arena_resource.h"
#include "services/stats_db/kernel_statistic_ids.h"
#include "services/stats_db/system_stats.h"

extern const unsigned int __static_heap_start;
extern const unsigned int __static_heap_size_in_bytes;
extern const unsigned int __dynamic_heap_start;
extern const unsigned int __dynamic_heap_size_in_bytes;
extern const unsigned int __filesystem_cache_heap_start;
extern const unsigned int __filesystem_cache_heap_size_in_bytes;

#define AARCH64_MEMORY_ALIGNMENT 8

minstd::pmr::lockfree_static_arena_resource __os_static_resource_core((uint8_t *)&__static_heap_start, STATIC_HEAP_SIZE_IN_BYTES);

namespace
{
    //  A count, not bytes: it stays right even if a caller frees with the wrong size.

    system_stats::KernelGaugeStatistic<system_stats::KernelStatisticId::HEAP_DYNAMIC_ALLOCATIONS>
        dynamic_allocations_gauge("heap", "dynamic_allocations", "Live allocations in the dynamic heap", 0);
}

class DynamicHeapProxyResource : public minstd::pmr::memory_resource {
public:
    explicit DynamicHeapProxyResource(bool counted)
        : counted_(counted)
    {
    }

    minstd::pmr::lockfree_composite_single_arena_resource<>* impl_ = nullptr;
protected:
    void* do_allocate(size_t bytes, size_t alignment) override {
        void* block = impl_ ? impl_->allocate(bytes, alignment) : nullptr;
        if (counted_ && (block != nullptr)) dynamic_allocations_gauge.Increment();
        return block;
    }
    void do_deallocate(void* p, size_t bytes, size_t alignment) override {
        if (impl_ == nullptr || p == nullptr) return;
        impl_->deallocate(p, bytes, alignment);
        if (counted_) dynamic_allocations_gauge.Increment(-1);
    }
    bool do_is_equal(minstd::pmr::memory_resource const& other) const noexcept override {
        return this == &other;
    }
private:
    const bool counted_;
};

DynamicHeapProxyResource __os_dynamic_resource_core_proxy(true);
DynamicHeapProxyResource __os_filesystem_cache_resource_core_proxy(false);

// We still export this pointer so modules dumping it (like show_diagnostics) can query it if they look for the real impl.
minstd::pmr::lockfree_composite_single_arena_resource<>* __os_dynamic_resource_core = nullptr;
minstd::pmr::lockfree_composite_single_arena_resource<>* __os_filesystem_cache_resource_core = nullptr;

minstd::pmr::memory_resource& __os_dynamic_heap_resource = __os_dynamic_resource_core_proxy;
minstd::pmr::memory_resource& __os_static_heap_resource = __os_static_resource_core;
minstd::pmr::memory_resource& __os_filesystem_cache_heap_resource = __os_filesystem_cache_resource_core_proxy;

dynamic_allocator<char> __dynamic_string_allocator;

extern "C" void initialize_dynamic_heap()
{
    void* resource_memory = __os_static_resource_core.allocate(
        sizeof(minstd::pmr::lockfree_composite_single_arena_resource<>),
        alignof(minstd::pmr::lockfree_composite_single_arena_resource<>)
    );

    __os_dynamic_resource_core = new (resource_memory) minstd::pmr::lockfree_composite_single_arena_resource<>(
        (uint8_t *)&__dynamic_heap_start, 
        DYNAMIC_HEAP_SIZE_IN_BYTES, 
        4, 
        4
    );

    __os_dynamic_resource_core_proxy.impl_ = __os_dynamic_resource_core;

    void* fs_cache_resource_memory = __os_static_resource_core.allocate(
        sizeof(minstd::pmr::lockfree_composite_single_arena_resource<>),
        alignof(minstd::pmr::lockfree_composite_single_arena_resource<>)
    );

    __os_filesystem_cache_resource_core = new (fs_cache_resource_memory) minstd::pmr::lockfree_composite_single_arena_resource<>(
        (uint8_t *)&__filesystem_cache_heap_start,
        FILESYSTEM_CACHE_HEAP_SIZE_IN_BYTES,
        4,
        4
    );

    __os_filesystem_cache_resource_core_proxy.impl_ = __os_filesystem_cache_resource_core;
}


