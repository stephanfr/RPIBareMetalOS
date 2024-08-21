// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include <type_traits>
#include <fixed_string>
#include <functional>
#include <list>
#include <memory>

#include "services/uuid.h"

#include "result.h"

typedef enum class OSEntityRegistryResultCodes
{
    SUCCESS = 1,
    NO_SUCH_ENTITY,
    ENTITY_ID_ALREADY_IN_USE,
    ENTITY_NAME_ALREADY_IN_USE,
    ENTITY_ALIAS_ALREADY_IN_USE,
    ERROR_SAVING_ENTITY_BY_UUID
} OSEntityRegistryResultCodes;

inline bool Successful(OSEntityRegistryResultCodes result_code)
{
    return result_code == OSEntityRegistryResultCodes::SUCCESS;
}

inline bool Failed(OSEntityRegistryResultCodes result_code)
{
    return result_code != OSEntityRegistryResultCodes::SUCCESS;
}

typedef enum class OSEntityTypes
{
    HARDWARE_RNG = 1,
    SOFTWARE_RNG,
    CHARACTER_DEVICE,
    BLOCK_DEVICE,
    FILESYSTEM,
    USER_INTERFACE
} OSEntityTypes;

class OSEntity
{
public:
    OSEntity(bool permanent,
             const char *name,
             const char *alias);

    virtual ~OSEntity(){};

    virtual OSEntityTypes OSEntityType() const noexcept = 0;

    const minstd::string &Name() const noexcept
    {
        return name_;
    }

    const minstd::string &Alias() const noexcept
    {
        return alias_;
    }

    const UUID &Id() const noexcept
    {
        return uuid_;
    }

    bool IsPermanent() const
    {
        return permanent_;
    }

    uint64_t NameHash() const
    {
        return name_hash_;
    }

    uint64_t AliasHash() const
    {
        return alias_hash_;
    }

private:
    const UUID uuid_;
    const bool permanent_;

    const minstd::fixed_string<MAX_OS_ENTITY_NAME_LENGTH> name_;
    const minstd::fixed_string<MAX_OS_ENTITY_NAME_LENGTH> alias_;

    const uint64_t name_hash_;
    const uint64_t alias_hash_;
};

class OSEntityRegistry
{
public:
    
    virtual bool DoesEntityExist( const UUID &id) = 0;
    virtual ReferenceResult<OSEntityRegistryResultCodes, OSEntity> GetEntityById(const UUID &id) = 0;

    virtual void FindEntitiesByType(OSEntityTypes type, minstd::list<UUID> &entity_ids) = 0;

    template <typename T>
    OSEntityRegistryResultCodes AddEntity(minstd::unique_ptr<T> &new_entity)
    {
        static_assert(minstd::is_base_of_v<OSEntity, T>);

        return AddEntityInternal((minstd::unique_ptr<OSEntity> &)new_entity);
    }

    OSEntityRegistryResultCodes RemoveEntityById( const UUID &id)
    {
        return RemoveEntityByIdInternal(id);
    }

    template <typename T>
    ReferenceResult<OSEntityRegistryResultCodes, T> GetEntityByName(const char *name)
    {
        static_assert(minstd::is_base_of_v<OSEntity, T>);

        auto get_entity_result = GetEntityByNameInternal(name);

        if (get_entity_result.Failed())
        {
            return ReferenceResult<OSEntityRegistryResultCodes, T>::Failure(get_entity_result.ResultCode());
        }

        return ReferenceResult<OSEntityRegistryResultCodes, T>::Success(static_cast<T &>(*get_entity_result));
    }

    template <typename T>
    ReferenceResult<OSEntityRegistryResultCodes, T> GetEntityByName(const minstd::string &name)
    {
        static_assert(minstd::is_base_of_v<OSEntity, T>);

        auto get_entity_result = GetEntityByNameInternal(name);

        if (get_entity_result.Failed())
        {
            return ReferenceResult<OSEntityRegistryResultCodes, T>::Failure(get_entity_result.ResultCode());
        }

        return ReferenceResult<OSEntityRegistryResultCodes, T>::Success(static_cast<T &>(*get_entity_result));
    }

    template <typename T>
    ReferenceResult<OSEntityRegistryResultCodes, T> GetEntityByAlias(const char *alias)
    {
        static_assert(minstd::is_base_of_v<OSEntity, T>);

        auto get_entity_result = GetEntityByAliasInternal(alias);

        if (get_entity_result.Failed())
        {
            return ReferenceResult<OSEntityRegistryResultCodes, T>::Failure(get_entity_result.ResultCode());
        }

        return ReferenceResult<OSEntityRegistryResultCodes, T>::Success(static_cast<T &>(*get_entity_result));
    }

    template <typename T>
    ReferenceResult<OSEntityRegistryResultCodes, T> GetEntityByAlias(const minstd::string &alias)
    {
        static_assert(minstd::is_base_of_v<OSEntity, T>);

        auto get_entity_result = GetEntityByAliasInternal(alias);

        if (get_entity_result.Failed())
        {
            return ReferenceResult<OSEntityRegistryResultCodes, T>::Failure(get_entity_result.ResultCode());
        }

        return ReferenceResult<OSEntityRegistryResultCodes, T>::Success(static_cast<T &>(*get_entity_result));
    }

protected:
    virtual OSEntityRegistryResultCodes AddEntityInternal(minstd::unique_ptr<OSEntity> &new_entity) = 0;
    virtual OSEntityRegistryResultCodes RemoveEntityByIdInternal(const UUID &id) = 0;

    virtual ReferenceResult<OSEntityRegistryResultCodes, OSEntity> GetEntityByNameInternal(const char *name) = 0;
    virtual ReferenceResult<OSEntityRegistryResultCodes, OSEntity> GetEntityByNameInternal(const minstd::string &name) = 0;
    virtual ReferenceResult<OSEntityRegistryResultCodes, OSEntity> GetEntityByAliasInternal(const char *alias) = 0;
    virtual ReferenceResult<OSEntityRegistryResultCodes, OSEntity> GetEntityByAliasInternal(const minstd::string &alias) = 0;
};

OSEntityRegistry &GetOSEntityRegistry();
