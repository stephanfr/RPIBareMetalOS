#include <memory>
#include "os_memory_config.h"
#include "asm_globals.h"
#include "heaps.h"
#include "synchronization.h"
#include "__memory_resource/composite_pool_resource.h"

extern const unsigned int __static_heap_start;
extern const unsigned int __static_heap_size_in_bytes;
extern const unsigned int __dynamic_heap_start;
extern const unsigned int __dynamic_heap_size_in_bytes;

#define AARCH64_MEMORY_ALIGNMENT 8

class MemoryResourceHeapAdapter : public minstd::memory_heap
{
    minstd::pmr::memory_resource& res_;
public:
    explicit MemoryResourceHeapAdapter(minstd::pmr::memory_resource& resource) : res_(resource) {}

    size_t bytes_in_use() const noexcept override { return 0; }
    size_t blocks_in_use() const noexcept override { return 0; }
    size_t bytes_reserved() const noexcept override { return 0; }
    size_t blocks_reserved() const noexcept override { return 0; }
    size_t raw_block_size(const void *block) const noexcept override { return 0; }
    size_t num_elements_in_block(const void *block) const noexcept override { return 1; }
    bool validate_pointer(const void *block) const noexcept override { return true; }
    size_t actual_block_size(const void *block) const noexcept override { return 0; }

protected:
    void *allocate_raw_block(size_t element_size_in_bytes, size_t num_elements) override
    {
        size_t total_size = (element_size_in_bytes * num_elements) + 8;
        void* p = res_.allocate(total_size, AARCH64_MEMORY_ALIGNMENT);
        if (p)
        {
            *(size_t*)p = total_size;
            return (char*)p + 8;
        }
        return nullptr;
    }
    
    void deallocate_raw_block(void *block) override
    {
        if (block)
        {
            void* orig = (char*)block - 8;
            size_t total_size = *(size_t*)orig;
            res_.deallocate(orig, total_size, AARCH64_MEMORY_ALIGNMENT);
        }
    }
};

minstd::pmr::composite_pool_resource<> __os_static_resource_core((uint8_t *)&__static_heap_start, STATIC_HEAP_SIZE_IN_BYTES, 1, 1);
minstd::pmr::composite_pool_resource<> __os_dynamic_resource_core((uint8_t *)&__dynamic_heap_start, DYNAMIC_HEAP_SIZE_IN_BYTES, 4, 4);

minstd::pmr::memory_resource& __os_dynamic_heap_resource = __os_dynamic_resource_core;
minstd::pmr::memory_resource& __os_static_heap_resource = __os_static_resource_core;

MemoryResourceHeapAdapter __os_static_heap_adapter(__os_static_resource_core);
MemoryResourceHeapAdapter __os_dynamic_heap_adapter(__os_dynamic_resource_core);

minstd::memory_heap &__os_static_heap = __os_static_heap_adapter;
minstd::memory_heap &__os_dynamic_heap = __os_dynamic_heap_adapter;
minstd::memory_heap &__os_filesystem_cache_heap = __os_dynamic_heap_adapter;

dynamic_allocator<char> __dynamic_string_allocator;
