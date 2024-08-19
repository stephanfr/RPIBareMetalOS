// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <avl_tree>
#include <dynamic_string>
#include <lru_cache>

#include "os_config.h"
#include "platform/mutex.h"
#include "platform/platform_sw_rngs.h"
#include "services/murmur_hash.h"

#include "heaps.h"

#include "filesystem/fat32_directory.h"

namespace filesystems::fat32
{

    template <typename T>
    class filesystem_cache_allocator : public minstd::heap_allocator<T>
    {
    public:
        filesystem_cache_allocator()
            : minstd::heap_allocator<T>(__os_filesystem_cache_heap)
        {
        }
    };

    template <typename T, typename... Args>
    inline minstd::unique_ptr<T> make_filesystem_cache_unique(Args &&...args)
    {
        T *temp = new (__os_dynamic_heap.allocate_block<T>(1)) T(minstd::forward<Args>(args)...);
        return minstd::unique_ptr<T>(temp, __os_filesystem_cache_heap);
    }

    typedef enum class FAT32DirectoryCacheEntryType : uint32_t
    {
        DIRECTORY = 1,
        FILE = 2
    } FAT32DirectoryCacheEntryType;

    class FAT32DirectoryCacheEntry
    {
    public:
        FAT32DirectoryCacheEntry(FAT32DirectoryCacheEntryType entry_type,
                                 const FAT32DirectoryEntryAddress &entry_address,
                                 const FAT32ClusterIndex first_cluster_id,
                                 const FAT32Compact8Dot3Filename &compact_name,
                                 const minstd::string &absolute_path,
                                 uint64_t path_hash)
            : entry_type_(entry_type),
              entry_address_(entry_address),
              first_cluster_id_(first_cluster_id),
              compact_name_(compact_name),
              absolute_path_(absolute_path, __filesystem_cache_string_allocator),
              path_hash_(path_hash)
        {
        }

        FAT32DirectoryCacheEntryType EntryType() const
        {
            return entry_type_;
        }

        const FAT32DirectoryEntryAddress &EntryAddress() const
        {
            return entry_address_;
        }

        FAT32ClusterIndex FirstClusterId() const
        {
            return first_cluster_id_;
        }

        const FAT32Compact8Dot3Filename &CompactName() const
        {
            return compact_name_;
        }

        const minstd::string &AbsolutePath() const
        {
            return absolute_path_;
        }

        uint64_t PathHash() const
        {
            return path_hash_;
        }

    private:
        inline static filesystem_cache_allocator<char> __filesystem_cache_string_allocator;

        FAT32DirectoryCacheEntry(const FAT32DirectoryCacheEntry &other) = delete;
        FAT32DirectoryCacheEntry &operator=(const FAT32DirectoryCacheEntry &other) = delete;

        const FAT32DirectoryCacheEntryType entry_type_;
        const FAT32DirectoryEntryAddress entry_address_;
        const FAT32ClusterIndex first_cluster_id_;
        const FAT32Compact8Dot3Filename compact_name_;
        const minstd::dynamic_string<MAX_FILESYSTEM_PATH_LENGTH> absolute_path_;
        const uint64_t path_hash_;
    };

    class FAT32DirectoryCache
    {
    public:
        FAT32DirectoryCache(size_t max_size, MurmurHash64ASeed seed = MurmurHash64ASeed(GetGeneralRNG().Next64BitValue()))
            : path_hash_seed_(seed),
              cache_(max_size, cache_list_allocator_, cache_map_allocator_),
              indices_by_path_hash_(indices_by_path_hash_allocator_)
        {
        }

        ~FAT32DirectoryCache()
        {
        }

        size_t MaxSize()
        {
            return cache_.max_size();
        }

        size_t CurrentSize()
        {
            return cache_.size();
        }

        void Clear()
        {
            cache_.clear();
            indices_by_path_hash_.clear();
        }

        uint64_t Hits() const noexcept
        {
            return hits_;
        }

        uint64_t Misses() const noexcept
        {
            return misses_;
        }

        uint64_t Collisions() const noexcept
        {
            return collisions_;
        }

        void AddEntry(FAT32DirectoryCacheEntryType entry_type,
                      const FAT32DirectoryEntryAddress &entry_address,
                      const FAT32ClusterIndex first_cluster_id,
                      const FAT32Compact8Dot3Filename &compact_name,
                      const minstd::string &path)
        {
            //  If the cluster index or the path hash already exist in their maps, then return immediately.
            //      The path hash could possibly exist even if the cluster index does not if there is a collision hashing the path

            uint64_t path_hash = MurmurHash64A(path.c_str(), path.length(), path_hash_seed_);

            if ((cache_.find(first_cluster_id).has_value()) ||
                (indices_by_path_hash_.find(path_hash)) != indices_by_path_hash_.end())
            {
                if (indices_by_path_hash_.find(path_hash) != indices_by_path_hash_.end())
                {
                    collisions_++;
                }

                return;
            }

            //  Insert the new entry

            if (cache_.add(first_cluster_id, make_filesystem_cache_unique<FAT32DirectoryCacheEntry>(entry_type, entry_address, first_cluster_id, compact_name, path, path_hash)))
            {
                indices_by_path_hash_.insert(path_hash, first_cluster_id);
            }
        }

        void RemoveEntry(FAT32ClusterIndex first_cluster_id)
        {
            auto entry = cache_.find(first_cluster_id);

            if (!entry.has_value())
            {
                return;
            }

            const minstd::string &path = entry->get()->AbsolutePath();

            uint64_t path_hash = MurmurHash64A(path.c_str(), path.length(), path_hash_seed_);

            if (cache_.remove(first_cluster_id))
            {
                indices_by_path_hash_.erase(path_hash);
            }
        }

        minstd::optional<minstd::reference_wrapper<FAT32DirectoryCacheEntry>> FindEntry(FAT32ClusterIndex first_cluster_id)
        {
            auto entry = cache_.find(first_cluster_id);

            if (!entry.has_value())
            {
                misses_++;

                return minstd::optional<minstd::reference_wrapper<FAT32DirectoryCacheEntry>>();
            }

            hits_++;

            return minstd::optional<minstd::reference_wrapper<FAT32DirectoryCacheEntry>>(minstd::reference_wrapper<FAT32DirectoryCacheEntry>(minstd::move(*(entry->get()))));
        }

        minstd::optional<FAT32ClusterIndex> FindFirstClusterIndex(const minstd::string &path)
        {
            uint64_t path_hash = MurmurHash64A(path.c_str(), path.length(), path_hash_seed_);

            auto itr = indices_by_path_hash_.find(path_hash);

            if (itr == indices_by_path_hash_.end())
            {
                misses_++;

                return minstd::optional<FAT32ClusterIndex>();
            }

            //  Insure paths match so we do not get a false match due to a hash collision

            auto entry = cache_.find(itr->second());

            if (entry->get()->AbsolutePath() != path)
            {
                collisions_++;

                return minstd::optional<FAT32ClusterIndex>();
            }

            hits_++;

            return minstd::optional<FAT32ClusterIndex>(itr->second());
        }

        minstd::optional<minstd::reference_wrapper<FAT32DirectoryCacheEntry>> FindEntry(const minstd::string &path)
        {
            uint64_t path_hash = MurmurHash64A(path.c_str(), path.length(), path_hash_seed_);

            auto itr = indices_by_path_hash_.find(path_hash);

            if (itr == indices_by_path_hash_.end())
            {
                misses_++;

                return minstd::optional<minstd::reference_wrapper<FAT32DirectoryCacheEntry>>();
            }

            //  Insure paths match so we do not get a false match due to a hash collision

            auto entry = cache_.find(itr->second());

            if (entry->get()->AbsolutePath() != path)
            {
                collisions_++;

                return minstd::optional<minstd::reference_wrapper<FAT32DirectoryCacheEntry>>();
            }

            //  We have the correct entry

            hits_++;

            return minstd::optional<minstd::reference_wrapper<FAT32DirectoryCacheEntry>>(minstd::reference_wrapper<FAT32DirectoryCacheEntry>(minstd::move(*(entry->get()))));
        }

    private:
        using EntryByFAT32ClusterIndexCache = minstd::lru_cache<FAT32ClusterIndex, minstd::unique_ptr<FAT32DirectoryCacheEntry>>;
        using EntryByFAT32ClusterIndexListAllocator = minstd::heap_allocator<EntryByFAT32ClusterIndexCache::list_entry_type>;
        using EntryByFAT32ClusterIndexMapAllocator = minstd::heap_allocator<EntryByFAT32ClusterIndexCache::map_entry_type>;

        using FAT32ClusterIndexByPathHashMap = minstd::avl_tree<uint64_t, FAT32ClusterIndex>;
        using FAT32ClusterIndexByPathHashMapAllocator = minstd::heap_allocator<FAT32ClusterIndexByPathHashMap::node_type>;

        const MurmurHash64ASeed path_hash_seed_;

        uint64_t hits_{0};
        uint64_t misses_{0};
        uint64_t collisions_{0};

        EntryByFAT32ClusterIndexListAllocator cache_list_allocator_{__os_filesystem_cache_heap};
        EntryByFAT32ClusterIndexMapAllocator cache_map_allocator_{__os_filesystem_cache_heap};
        EntryByFAT32ClusterIndexCache cache_;

        FAT32ClusterIndexByPathHashMapAllocator indices_by_path_hash_allocator_{__os_filesystem_cache_heap};
        FAT32ClusterIndexByPathHashMap indices_by_path_hash_{indices_by_path_hash_allocator_};
    };
} // namespace filesystems::fat32
