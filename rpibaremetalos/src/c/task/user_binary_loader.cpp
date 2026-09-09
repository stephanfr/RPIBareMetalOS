#include "task/user_binary_loader.h"

#include <string.h>

#include <buffer>
#include <list>
#include <__memory_resource/monotonic_buffer_resource.h>
#include <__memory_resource/polymorphic_allocator.h>

#include "os_entity.h"

#include "filesystem/filesystems.h"

#include "devices/log.h"
#include "heaps.h"
#include "platform/memory_model.h"

namespace task
{
    namespace
    {
        constexpr uint32_t MAX_FILESYSTEMS_TO_SEARCH = 8;
        constexpr uint64_t CACHE_LINE_SIZE = 64;

        constexpr uint64_t RoundUpToPage(uint64_t value)
        {
            return (value + BYTES_4K - 1) & ~(BYTES_4K - 1);
        }

        //  minstd::buffer over memory we already own.  File::Read() takes a buffer<uint8_t>&,
        //      and the image goes into a page block from the model's frame hook rather than
        //      the dynamic heap -- a user binary can be far larger than a heap allocation
        //      should be.

        class raw_buffer : public minstd::buffer<uint8_t>
        {
        public:
            raw_buffer(uint8_t *data, size_t capacity)
                : data_(data), capacity_(capacity), size_(0)
            {
            }

            size_t buffer_size() const override { return capacity_; }
            size_t size() const override { return size_; }
            size_t space_remaining() const override { return capacity_ - size_; }

            void clear() override { size_ = 0; }

            uint8_t &operator[](size_t index) override { return data_[index]; }
            const uint8_t &operator[](size_t index) const override { return data_[index]; }

            uint8_t *data() override { return data_; }
            const uint8_t *data() const override { return data_; }

            size_t append(uint8_t element) override
            {
                if (size_ < capacity_)
                {
                    data_[size_++] = element;
                }

                return size_;
            }

            size_t append(const uint8_t *block, size_t count) override
            {
                if (count > capacity_ - size_)
                {
                    count = capacity_ - size_;
                }

                memcpy(data_ + size_, block, count);
                size_ += count;

                return size_;
            }

            void pop_back() override
            {
                if (size_ > 0)
                {
                    size_--;
                }
            }

        private:
            uint8_t *data_;
            size_t capacity_;
            size_t size_;
        };

        //  The boot partition is where the build's mcopy puts the user binary, alongside
        //      kernel8.img and cmdline.txt.

        filesystems::Filesystem *FindBootFilesystem()
        {
            using uuid_node_type = minstd::list<UUID>::node_type;

            alignas(uuid_node_type) uint8_t list_buffer[sizeof(uuid_node_type) * MAX_FILESYSTEMS_TO_SEARCH +
                                                        alignof(uuid_node_type) * MAX_FILESYSTEMS_TO_SEARCH];
            minstd::pmr::monotonic_buffer_resource list_resource(list_buffer, sizeof(list_buffer), nullptr);
            minstd::pmr::polymorphic_allocator<uuid_node_type> list_allocator(&list_resource);
            minstd::list<UUID> filesystem_ids(list_allocator);

            GetOSEntityRegistry().FindEntitiesByType(OSEntityTypes::FILESYSTEM, filesystem_ids);

            for (const auto &filesystem_id : filesystem_ids)
            {
                auto entity = GetOSEntityRegistry().GetEntityById(filesystem_id);

                if (entity.Failed())
                {
                    continue;
                }

                auto &filesystem = static_cast<filesystems::Filesystem &>(entity);

                if (filesystem.IsBoot())
                {
                    return &filesystem;
                }
            }

            return nullptr;
        }

        //  The I-cache is PIPT on A53/A72/A76, so cleaning and invalidating through ANY
        //      alias of the physical page works -- here, the kernel VA we copied through.
        //      Without this a board can execute stale I-cache contents on the first run;
        //      QEMU never shows it.

        void SynchronizeInstructionCache(uint64_t kernel_va, uint64_t length)
        {
            for (uint64_t address = kernel_va; address < kernel_va + length; address += CACHE_LINE_SIZE)
            {
                asm volatile("dc cvau, %0" ::"r"(address) : "memory");
                asm volatile("ic ivau, %0" ::"r"(address) : "memory");
            }

            asm volatile("dsb ish" ::: "memory");
            asm volatile("isb" ::: "memory");
        }
    }

        ValueResult<TaskResultCodes, uint64_t> LoadUserBinary(const minstd::string &path, AddressSpace &address_space)
    {
        using Result = ValueResult<TaskResultCodes, uint64_t>;

        const MemoryModel &model = MemoryModel::Instance();
        const UserSpaceLayout &layout = model.UserSpace();

        filesystems::Filesystem *filesystem = FindBootFilesystem();

        if (filesystem == nullptr)
        {
            LogError("LoadUserBinary: no boot filesystem is mounted\n");
            return Result::Failure(TaskResultCodes::USER_BINARY_NOT_FOUND);
        }

        auto root_directory = filesystem->GetRootDirectory();

        if (root_directory.Failed())
        {
            return Result::Failure(TaskResultCodes::USER_BINARY_NOT_FOUND);
        }

        auto file = root_directory->OpenFile(path, filesystems::FileModes::READ);

        if (file.Failed())
        {
            LogError("LoadUserBinary: cannot open '%s'\n", path.c_str());
            return Result::Failure(TaskResultCodes::USER_BINARY_NOT_FOUND);
        }

        auto file_size = file->Size();

        if (file_size.Failed() || (file_size.Value() < UserImageHeader::SIZE))
        {
            return Result::Failure(TaskResultCodes::USER_BINARY_MALFORMED);
        }

        const uint64_t image_size = file_size.Value();

        //  Read the whole file into a kernel buffer, dereferenced through the kernel VA.

        MemoryPagePointer file_block = model.AllocateUserFrame(RoundUpToPage(image_size));

        if (file_block == 0)
        {
            return Result::Failure(TaskResultCodes::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK);
        }

        uint8_t *image = (uint8_t *)file_block;
        raw_buffer image_buffer(image, RoundUpToPage(image_size));

        if (file->Read(image_buffer) != filesystems::FilesystemResultCodes::SUCCESS)
        {
            model.ReleaseUserFrame(file_block, RoundUpToPage(image_size));
            return Result::Failure(TaskResultCodes::USER_BINARY_UNREADABLE);
        }

        file->Close();

        //  Validate before mapping anything.

        const UserImageHeader *header = (const UserImageHeader *)image;

        const uint64_t text_size = header->text_size;
        const uint64_t data_size = header->data_size;
        const uint64_t bss_size = header->bss_size;
        const uint64_t data_span = RoundUpToPage(data_size + bss_size);

        const bool valid = (header->magic == UserImageHeader::MAGIC) &&
                           ((text_size & (BYTES_4K - 1)) == 0) &&
                           (text_size >= UserImageHeader::SIZE) &&
                           (text_size + data_size == image_size) &&
                           (text_size + data_span <= layout.heap_base - layout.image_base);

        if (!valid)
        {
            LogError("LoadUserBinary: '%s' is not a valid user image\n", path.c_str());
            model.ReleaseUserFrame(file_block, RoundUpToPage(image_size));
            return Result::Failure(TaskResultCodes::USER_BINARY_MALFORMED);
        }

        //  Text: header + code + rodata, mapped read-only and EL0-executable.

        MemoryPagePointer text_block = model.AllocateUserFrame(text_size);

        if (text_block == 0)
        {
            model.ReleaseUserFrame(file_block, RoundUpToPage(image_size));
            return Result::Failure(TaskResultCodes::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK);
        }

        memcpy((uint8_t *)text_block, image, text_size);
        SynchronizeInstructionCache((uint64_t)(uint8_t *)text_block, text_size);

        if (!address_space.MapPages(layout.image_base, text_block.Physical(), text_size,
                                    Stage2AccessPermission::EL1_READ_ONLY_EL0_READ_ONLY, true))
        {
            model.ReleaseUserFrame(text_block, text_size);
            model.ReleaseUserFrame(file_block, RoundUpToPage(image_size));
            return Result::Failure(TaskResultCodes::UNABLE_TO_MAP_USER_BINARY);
        }

        //  Data + bss: one block, zeroed, with the initialised bytes copied over the front.

        if (data_span > 0)
        {
            MemoryPagePointer data_block = model.AllocateUserFrame(data_span);

            if (data_block == 0)
            {
                model.ReleaseUserFrame(file_block, RoundUpToPage(image_size));
                return Result::Failure(TaskResultCodes::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK);
            }

            uint8_t *data = (uint8_t *)data_block;

            memset(data, 0, data_span);

            if (data_size > 0)
            {
                memcpy(data, image + text_size, data_size);
            }

            if (!address_space.MapPages(layout.image_base + text_size, data_block.Physical(), data_span,
                                        Stage2AccessPermission::EL1_READ_WRITE_EL0_READ_WRITE, false))
            {
                model.ReleaseUserFrame(data_block, data_span);
                model.ReleaseUserFrame(file_block, RoundUpToPage(image_size));
                return Result::Failure(TaskResultCodes::UNABLE_TO_MAP_USER_BINARY);
            }
        }

        model.ReleaseUserFrame(file_block, RoundUpToPage(image_size));

        return Result::Success(layout.image_base + UserImageHeader::SIZE);
    }
} // namespace task