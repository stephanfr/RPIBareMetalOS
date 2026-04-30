#include <memory>
#include "os_memory_config.h"
#include "asm_globals.h"
#include "heaps.h"
#include "synchronization.h"
#include "__memory_resource/lockfree_composite_single_arena_resource.h"
#include "__memory_resource/lockfree_static_arena_resource.h"

extern const unsigned int __static_heap_start;
extern const unsigned int __static_heap_size_in_bytes;
extern const unsigned int __dynamic_heap_start;
extern const unsigned int __dynamic_heap_size_in_bytes;
extern const unsigned int __filesystem_cache_heap_start;
extern const unsigned int __filesystem_cache_heap_size_in_bytes;

#define AARCH64_MEMORY_ALIGNMENT 8

minstd::pmr::lockfree_static_arena_resource __os_static_resource_core((uint8_t *)&__static_heap_start, STATIC_HEAP_SIZE_IN_BYTES);

class DynamicHeapProxyResource : public minstd::pmr::memory_resource {
public:
    minstd::pmr::lockfree_composite_single_arena_resource<>* impl_ = nullptr;
protected:
    void* do_allocate(size_t bytes, size_t alignment) override {
        return impl_ ? impl_->allocate(bytes, alignment) : nullptr;
    }
    void do_deallocate(void* p, size_t bytes, size_t alignment) override {
        if (impl_) impl_->deallocate(p, bytes, alignment);
    }
    bool do_is_equal(minstd::pmr::memory_resource const& other) const noexcept override {
        return this == &other;
    }
};

DynamicHeapProxyResource __os_dynamic_resource_core_proxy;
DynamicHeapProxyResource __os_filesystem_cache_resource_core_proxy;

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


