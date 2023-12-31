// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "os_entity.h"

#include <heap_allocator>
#include <list>
#include <map>
#include <minstd_utility.h>

#include "memory.h"

#include "services/murmur_hash.h"

#include "devices/log.h"

uint64_t __os_entity_hash_seed = 0;

inline uint64_t ComputeHash(const minstd::string &name_or_alias)
{
    return MurmurHash64A(name_or_alias.c_str(), name_or_alias.length(), __os_entity_hash_seed);
}

inline uint64_t ComputeHash(const char *name_or_alias)
{
    uint32_t length = strnlen(name_or_alias, MAX_FILENAME_LENGTH);

    return MurmurHash64A(name_or_alias, length, __os_entity_hash_seed);
}

//
//  Constructor for OSEntity
//

OSEntity::OSEntity(bool permanent,
                   const char *name,
                   const char *alias)
    : uuid_(UUID::GenerateUUID(UUID::Versions::RANDOM)),
      permanent_(permanent),
      name_(name),
      alias_(alias),
      name_hash_(ComputeHash(name_)),
      alias_hash_(ComputeHash(alias_))
{
}

//
//  OSEntityRegistryImpl
//

class OSEntityRegistryImpl : public OSEntityRegistry
{
public:
    OSEntityRegistryImpl()
    {
    }

    ReferenceResult<OSEntityRegistryResultCodes, OSEntity> GetEntityById(UUID id) override;

    void FindEntitiesByType(OSEntityTypes type, minstd::list<UUID> &entity_ids) override;

protected:
    
    OSEntityRegistryResultCodes AddEntityInternal(unique_ptr<OSEntity> &new_entity) override;

    ReferenceResult<OSEntityRegistryResultCodes, OSEntity> GetEntityByNameInternal(const char *name) override
    {
        return GetEntityByNameHash(ComputeHash(name));
    }

    ReferenceResult<OSEntityRegistryResultCodes, OSEntity> GetEntityByNameInternal(const minstd::string &name) override
    {
        return GetEntityByNameHash(ComputeHash(name.c_str()));
    }

    ReferenceResult<OSEntityRegistryResultCodes, OSEntity> GetEntityByAliasInternal(const char *alias) override
    {
        return GetEntityByAliasHash(ComputeHash(alias));
    }

    ReferenceResult<OSEntityRegistryResultCodes, OSEntity> GetEntityByAliasInternal(const minstd::string &alias) override
    {
        return GetEntityByAliasHash(ComputeHash(alias.c_str()));
    }

private:
    using EntityByUUIDMapStaticHeapAllocator = minstd::heap_allocator<minstd::map<UUID, unique_ptr<OSEntity>>::node_type>;
    using EntityUUIDByHashMapStaticHeapAllocator = minstd::heap_allocator<minstd::map<uint64_t, UUID>::node_type>;

    EntityByUUIDMapStaticHeapAllocator entity_by_id_map_element_allocator_ = EntityByUUIDMapStaticHeapAllocator(__os_static_heap);
    minstd::map<UUID, unique_ptr<OSEntity>> entity_by_id_ = minstd::map<UUID, unique_ptr<OSEntity>>(entity_by_id_map_element_allocator_);

    EntityUUIDByHashMapStaticHeapAllocator entity_id_by_hash_element_allocator_ = EntityUUIDByHashMapStaticHeapAllocator(__os_static_heap);
    minstd::map<uint64_t, UUID> entity_id_by_name_hash_ = minstd::map<uint64_t, UUID>(entity_id_by_hash_element_allocator_);
    minstd::map<uint64_t, UUID> entity_id_by_alias_hash_ = minstd::map<uint64_t, UUID>(entity_id_by_hash_element_allocator_);

    ReferenceResult<OSEntityRegistryResultCodes, OSEntity> GetEntityByNameHash(uint64_t name_hash);
    ReferenceResult<OSEntityRegistryResultCodes, OSEntity> GetEntityByAliasHash(uint64_t alias_hash);
};

OSEntityRegistryResultCodes OSEntityRegistryImpl::AddEntityInternal(unique_ptr<OSEntity> &new_entity)
{
    LogDebug1("Adding Entity with Name: %s\n", new_entity->Name().c_str());

    //  Insure the id and name is not already in use.  There is a super-small risk we have a name collision...

    if (entity_by_id_.find(new_entity->Id()) != entity_by_id_.end())
    {
        return OSEntityRegistryResultCodes::ENTITY_ID_ALREADY_IN_USE;
    }

    if (entity_id_by_name_hash_.find(new_entity->NameHash()) != entity_id_by_name_hash_.end())
    {
        return OSEntityRegistryResultCodes::ENTITY_NAME_ALREADY_IN_USE;
    }

    if (entity_id_by_alias_hash_.find(new_entity->AliasHash()) != entity_id_by_alias_hash_.end())
    {
        return OSEntityRegistryResultCodes::ENTITY_ALIAS_ALREADY_IN_USE;
    }

    //  Insert the device into the the devices by id map

    auto insert_entity_result = entity_by_id_.insert(new_entity->Id(), minstd::move(new_entity));

    if (!insert_entity_result.second())
    {
        return OSEntityRegistryResultCodes::ERROR_SAVING_ENTITY_BY_UUID;
    }

    //  Insert the device id into the map indexed by name and also by alias

    entity_id_by_name_hash_.insert(insert_entity_result.first()->second()->NameHash(), insert_entity_result.first()->second()->Id());
    entity_id_by_alias_hash_.insert(insert_entity_result.first()->second()->AliasHash(), insert_entity_result.first()->second()->Id());

    //  Success

    return OSEntityRegistryResultCodes::SUCCESS;
}

ReferenceResult<OSEntityRegistryResultCodes, OSEntity> OSEntityRegistryImpl::GetEntityById(UUID id)
{
    using Result = ReferenceResult<OSEntityRegistryResultCodes, OSEntity>;

    auto entity_itr = entity_by_id_.find(id);

    if (entity_itr == entity_by_id_.end())
    {
        return Result::Failure(OSEntityRegistryResultCodes::NO_SUCH_ENTITY);
    }

    return Result::Success(*(entity_itr->second()));
}

inline ReferenceResult<OSEntityRegistryResultCodes, OSEntity> OSEntityRegistryImpl::GetEntityByNameHash(uint64_t name_hash)
{
    using Result = ReferenceResult<OSEntityRegistryResultCodes, OSEntity>;

    auto id_itr = entity_id_by_name_hash_.find(name_hash);

    if (id_itr == entity_id_by_name_hash_.end())
    {
        return Result::Failure(OSEntityRegistryResultCodes::NO_SUCH_ENTITY);
    }

    auto entity_itr = entity_by_id_.find(id_itr->second());

    if (entity_itr == entity_by_id_.end())
    {
        return Result::Failure(OSEntityRegistryResultCodes::NO_SUCH_ENTITY);
    }

    return Result::Success(*(entity_itr->second()));
}

inline ReferenceResult<OSEntityRegistryResultCodes, OSEntity> OSEntityRegistryImpl::GetEntityByAliasHash(uint64_t alias_hash)
{
    using Result = ReferenceResult<OSEntityRegistryResultCodes, OSEntity>;

    auto id_itr = entity_id_by_alias_hash_.find(alias_hash);

    if (id_itr == entity_id_by_alias_hash_.end())
    {
        return Result::Failure(OSEntityRegistryResultCodes::NO_SUCH_ENTITY);
    }

    auto entity_itr = entity_by_id_.find(id_itr->second());

    if (entity_itr == entity_by_id_.end())
    {
        return Result::Failure(OSEntityRegistryResultCodes::NO_SUCH_ENTITY);
    }

    return Result::Success(*(entity_itr->second()));
}

void OSEntityRegistryImpl::FindEntitiesByType(OSEntityTypes type, minstd::list<UUID> &entity_ids)
{
    //    entity_ids.clear();

    for (auto itr = entity_by_id_.begin(); itr != entity_by_id_.end(); itr++)
    {
        if (itr->second()->OSEntityType() == type)
        {
            entity_ids.push_back(itr->first());
        }
    }
}

//
//  Global Instance
//

static OSEntityRegistryImpl *__os_entity_registry = nullptr;

OSEntityRegistry &GetOSEntityRegistry()
{
    if (__os_entity_registry == nullptr)
    {
        __os_entity_registry = static_new<OSEntityRegistryImpl>();
    }

    return *__os_entity_registry;
}
