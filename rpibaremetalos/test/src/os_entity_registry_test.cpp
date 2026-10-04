// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "os_entity.h"

#include <list>
#include <string.h>

//  Provides dynamic_new<T>() used to build the real unique_ptrs that AddEntity
//  takes.  The heap resources it references are declared extern there and
//  defined in cpputest_main.cpp.
#include "heaps.h"

namespace
{
    //
    //  Concrete OSEntity used throughout the registry tests.
    //
    template <OSEntityTypes ENTITY_TYPE>
    class TestOSEntity : public OSEntity
    {
    public:
        TestOSEntity(bool permanent, const char *name, const char *alias)
            : OSEntity(permanent, name, alias)
        {
        }

        OSEntityTypes OSEntityType() const noexcept override
        {
            return ENTITY_TYPE;
        }
    };

    using CharacterDeviceEntity = TestOSEntity<OSEntityTypes::CHARACTER_DEVICE>;
    using BlockDeviceEntity = TestOSEntity<OSEntityTypes::BLOCK_DEVICE>;
    using FilesystemEntity = TestOSEntity<OSEntityTypes::FILESYSTEM>;

    //
    //  The registry is a process-wide singleton and OSEntity ids come from the
    //  process-wide UUID RNG, so every test that creates entities starts by
    //  seeding the RNG with its own fixed constant.  That makes each entity id
    //  deterministic and independent of which other tests ran first (or whether
    //  this file's groups run alone), and keeps the id spaces of different
    //  tests from overlapping in the shared registry.
    //

    //
    //  The string-taking GetEntityByName / GetEntityByAlias overloads take a
    //  const minstd::string &.  minstd::fixed_string is the concrete string
    //  type in this codebase and upcasts to minstd::string implicitly.
    //
    using TestEntityName = minstd::fixed_string<MAX_OS_ENTITY_NAME_LENGTH>;

    //
    //  Local check helpers: CppUTest's CHECK_EQUAL cannot print scoped enums,
    //  so every comparison below goes through an explicit int cast.
    //
    template <typename ResultType>
    void CheckFailedWithCode(OSEntityRegistryResultCodes expected_code, const ResultType &result)
    {
        CHECK_TRUE(result.Failed());
        CHECK_EQUAL(static_cast<int>(expected_code), static_cast<int>(result.ResultCode()));
    }

    void CheckCodeEquals(OSEntityRegistryResultCodes expected_code, OSEntityRegistryResultCodes actual_code)
    {
        CHECK_EQUAL(static_cast<int>(expected_code), static_cast<int>(actual_code));
    }

    // ============================================================================
    //  OSEntity Accessor Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(OSEntityAccessorTests)
    {};
#pragma GCC diagnostic pop

    TEST(OSEntityAccessorTests, NameAndAliasAreReported)
    {
        UUID::SeedRNG(0x0BADC0DE00000001ULL);
        auto entity = dynamic_new<CharacterDeviceEntity>(true, "test-entity", "t-e");

        STRCMP_EQUAL("test-entity", entity->Name().c_str());
        STRCMP_EQUAL("t-e", entity->Alias().c_str());
    }

    TEST(OSEntityAccessorTests, PermanentFlagIsReported)
    {
        UUID::SeedRNG(0x0BADC0DE00000002ULL);
        auto permanent = dynamic_new<CharacterDeviceEntity>(true, "perm-name", "perm-alias");
        auto ephemeral = dynamic_new<CharacterDeviceEntity>(false, "ephem-name", "ephem-alias");

        CHECK_TRUE(permanent->IsPermanent());
        CHECK_FALSE(ephemeral->IsPermanent());
    }

    TEST(OSEntityAccessorTests, GeneratedIdIsRandomVersionUuid)
    {
        UUID::SeedRNG(0x0BADC0DE00000003ULL);
        auto entity = dynamic_new<CharacterDeviceEntity>(true, "uuid-name", "uuid-alias");

        CHECK_TRUE(entity->Id() != UUID::NIL);
        CHECK_EQUAL(static_cast<int>(UUID::Versions::RANDOM), static_cast<int>(entity->Id().Version()));
    }

    TEST(OSEntityAccessorTests, EntityTypesAreReportedBySubclass)
    {
        UUID::SeedRNG(0x0BADC0DE00000004ULL);
        auto character = dynamic_new<CharacterDeviceEntity>(true, "type-name-1", "type-alias-1");
        auto block = dynamic_new<BlockDeviceEntity>(true, "type-name-2", "type-alias-2");
        auto filesystem = dynamic_new<FilesystemEntity>(true, "type-name-3", "type-alias-3");

        CHECK_EQUAL(static_cast<int>(OSEntityTypes::CHARACTER_DEVICE), static_cast<int>(character->OSEntityType()));
        CHECK_EQUAL(static_cast<int>(OSEntityTypes::BLOCK_DEVICE), static_cast<int>(block->OSEntityType()));
        CHECK_EQUAL(static_cast<int>(OSEntityTypes::FILESYSTEM), static_cast<int>(filesystem->OSEntityType()));
    }

    TEST(OSEntityAccessorTests, HashesAreDeterministicForSameInput)
    {
        UUID::SeedRNG(0x0BADC0DE00000005ULL);
        auto first = dynamic_new<CharacterDeviceEntity>(true, "hash-name", "hash-alias");
        auto second = dynamic_new<CharacterDeviceEntity>(true, "hash-name", "hash-alias");

        CHECK_EQUAL(first->NameHash(), second->NameHash());
        CHECK_EQUAL(first->AliasHash(), second->AliasHash());
    }

    TEST(OSEntityAccessorTests, DifferentNamesProduceDifferentHashes)
    {
        UUID::SeedRNG(0x0BADC0DE00000006ULL);

        auto first = dynamic_new<CharacterDeviceEntity>(true, "hash-name-a", "same-alias");
        auto second = dynamic_new<CharacterDeviceEntity>(true, "hash-name-b", "same-alias");

        CHECK_TRUE(first->NameHash() != second->NameHash());
        CHECK_TRUE(first->AliasHash() == second->AliasHash());
    }

    TEST(OSEntityAccessorTests, LongNamesTruncateToEntityNameLimit)
    {
        UUID::SeedRNG(0x0BADC0DE00000007ULL);

        //  MAX_OS_ENTITY_NAME_LENGTH is 64; a longer literal must be cut down.
        const char long_name[] = "n1234567890123456789012345678901234567890123456789012345678901234567890";
        auto entity = dynamic_new<CharacterDeviceEntity>(true, long_name, "trunc-alias");

        CHECK_EQUAL(MAX_OS_ENTITY_NAME_LENGTH, entity->Name().length());
    }

    // ============================================================================
    //  OSEntityRegistry Singleton Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(OSEntityRegistrySingletonTests)
    {};
#pragma GCC diagnostic pop

    TEST(OSEntityRegistrySingletonTests, ReturnsSameInstanceOnRepeatedCalls)
    {
        OSEntityRegistry &first = GetOSEntityRegistry();
        OSEntityRegistry &second = GetOSEntityRegistry();

        POINTERS_EQUAL(&first, &second);
    }

    // ============================================================================
    //  Add / Retrieve By ID Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(OSEntityAddAndRetrieveByIdTests)
    {};
#pragma GCC diagnostic pop

    TEST(OSEntityAddAndRetrieveByIdTests, AddedEntityIsFoundById)
    {
        OSEntityRegistry &registry = GetOSEntityRegistry();

        UUID::SeedRNG(0x5EEDADD100000001ULL);
        auto entity = dynamic_new<CharacterDeviceEntity>(true, "add-id-name", "add-id-alias");
        UUID id = entity->Id();
        //  AddEntity moves ownership of the unique_ptr into the registry.
        CharacterDeviceEntity *entity_ptr = entity.get();

        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(entity)));

        CHECK_TRUE(registry.DoesEntityExist(id));

        auto result = registry.GetEntityById(id);
        CHECK_TRUE(result.Successful());
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS), static_cast<int>(result.ResultCode()));
        POINTERS_EQUAL(entity_ptr, &result.Value());
    }

    TEST(OSEntityAddAndRetrieveByIdTests, UnknownIdIsNotFound)
    {
        OSEntityRegistry &registry = GetOSEntityRegistry();

        UUID::SeedRNG(0x5EEDADD100000002ULL);
        UUID unknown = UUID::GenerateUUID(UUID::Versions::RANDOM);

        CHECK_FALSE(registry.DoesEntityExist(unknown));

        auto result = registry.GetEntityById(unknown);
        CheckFailedWithCode(OSEntityRegistryResultCodes::NO_SUCH_ENTITY, result);
    }

    TEST(OSEntityAddAndRetrieveByIdTests, EachAddedEntityGetsADistinctId)
    {
        OSEntityRegistry &registry = GetOSEntityRegistry();

        UUID::SeedRNG(0x5EEDADD100000003ULL);
        auto first = dynamic_new<CharacterDeviceEntity>(true, "distinct-name-1", "distinct-alias-1");
        auto second = dynamic_new<CharacterDeviceEntity>(true, "distinct-name-2", "distinct-alias-2");
        UUID first_id = first->Id();
        UUID second_id = second->Id();

        CHECK_TRUE(first_id != second_id);

        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(first)));
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(second)));

        CHECK_TRUE(registry.DoesEntityExist(first_id));
        CHECK_TRUE(registry.DoesEntityExist(second_id));
    }

    // ============================================================================
    //  Duplicate Name / Alias Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(OSEntityDuplicateNameAndAliasTests)
    {};
#pragma GCC diagnostic pop

    TEST(OSEntityDuplicateNameAndAliasTests, DuplicateNameIsRejected)
    {
        OSEntityRegistry &registry = GetOSEntityRegistry();

        UUID::SeedRNG(0x5EEDD00100000001ULL);
        auto first = dynamic_new<CharacterDeviceEntity>(true, "dup-name", "dup-alias-a");
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(first)));

        auto duplicate = dynamic_new<CharacterDeviceEntity>(true, "dup-name", "dup-alias-b");
        CheckCodeEquals(OSEntityRegistryResultCodes::ENTITY_NAME_ALREADY_IN_USE,
                        registry.AddEntity(duplicate));
    }

    TEST(OSEntityDuplicateNameAndAliasTests, DuplicateAliasIsRejected)
    {
        OSEntityRegistry &registry = GetOSEntityRegistry();

        UUID::SeedRNG(0x5EEDD00100000002ULL);
        auto first = dynamic_new<CharacterDeviceEntity>(true, "dup-alias-name-a", "dup-alias");
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(first)));

        auto duplicate = dynamic_new<CharacterDeviceEntity>(true, "dup-alias-name-b", "dup-alias");
        CheckCodeEquals(OSEntityRegistryResultCodes::ENTITY_ALIAS_ALREADY_IN_USE,
                        registry.AddEntity(duplicate));
    }

    TEST(OSEntityDuplicateNameAndAliasTests, RejectedEntityIsNotRegistered)
    {
        OSEntityRegistry &registry = GetOSEntityRegistry();

        UUID::SeedRNG(0x5EEDD00100000003ULL);
        auto first = dynamic_new<CharacterDeviceEntity>(true, "reject-name", "reject-alias");
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(first)));

        auto duplicate = dynamic_new<CharacterDeviceEntity>(true, "reject-name", "other-alias");
        UUID duplicate_id = duplicate->Id();
        CheckCodeEquals(OSEntityRegistryResultCodes::ENTITY_NAME_ALREADY_IN_USE,
                        registry.AddEntity(duplicate));

        CHECK_FALSE(registry.DoesEntityExist(duplicate_id));
        CheckFailedWithCode(OSEntityRegistryResultCodes::NO_SUCH_ENTITY,
                               registry.GetEntityById(duplicate_id));
    }

    // ============================================================================
    //  Get By Name / Alias Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(OSEntityGetNameAndAliasTests)
    {};
#pragma GCC diagnostic pop

    TEST(OSEntityGetNameAndAliasTests, GetNameByCStringFindsEntity)
    {
        OSEntityRegistry &registry = GetOSEntityRegistry();

        UUID::SeedRNG(0x5EED4A4D00000001ULL);
        auto entity = dynamic_new<CharacterDeviceEntity>(true, "get-name-cstr", "get-name-alias");
        CharacterDeviceEntity *entity_ptr = entity.get();  //  AddEntity moves ownership.
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(entity)));

        auto result = registry.GetEntityByName<CharacterDeviceEntity>("get-name-cstr");
        CHECK_TRUE(result.Successful());
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS), static_cast<int>(result.ResultCode()));
        POINTERS_EQUAL(entity_ptr, &result.Value());
    }

    TEST(OSEntityGetNameAndAliasTests, GetNameByStdStringFindsEntity)
    {
        OSEntityRegistry &registry = GetOSEntityRegistry();

        UUID::SeedRNG(0x5EED4A4D00000002ULL);

        auto entity = dynamic_new<CharacterDeviceEntity>(true, "get-name-std", "get-name-std-alias");
        CharacterDeviceEntity *entity_ptr = entity.get();  //  AddEntity moves ownership.
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(entity)));

        TestEntityName name("get-name-std");

        auto result = registry.GetEntityByName<CharacterDeviceEntity>(name);
        CHECK_TRUE(result.Successful());
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS), static_cast<int>(result.ResultCode()));
        POINTERS_EQUAL(entity_ptr, &result.Value());
    }

    TEST(OSEntityGetNameAndAliasTests, GetAliasByCStringFindsEntity)
    {
        OSEntityRegistry &registry = GetOSEntityRegistry();

        UUID::SeedRNG(0x5EED4A4D00000003ULL);
        auto entity = dynamic_new<CharacterDeviceEntity>(true, "get-alias-name", "get-alias-cstr");
        CharacterDeviceEntity *entity_ptr = entity.get();  //  AddEntity moves ownership.
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(entity)));

        auto result = registry.GetEntityByAlias<CharacterDeviceEntity>("get-alias-cstr");
        CHECK_TRUE(result.Successful());
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS), static_cast<int>(result.ResultCode()));
        POINTERS_EQUAL(entity_ptr, &result.Value());
    }

    TEST(OSEntityGetNameAndAliasTests, GetAliasByStdStringFindsEntity)
    {
        OSEntityRegistry &registry = GetOSEntityRegistry();

        UUID::SeedRNG(0x5EED4A4D00000004ULL);
        auto entity = dynamic_new<CharacterDeviceEntity>(true, "get-alias-std-name", "get-alias-std");
        CharacterDeviceEntity *entity_ptr = entity.get();  //  AddEntity moves ownership.
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(entity)));

        TestEntityName alias("get-alias-std");

        auto result = registry.GetEntityByAlias<CharacterDeviceEntity>(alias);
        CHECK_TRUE(result.Successful());
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS), static_cast<int>(result.ResultCode()));
        POINTERS_EQUAL(entity_ptr, &result.Value());
    }

    TEST(OSEntityGetNameAndAliasTests, NameLookupDoesNotMatchAliasAndViceVersa)
    {
        OSEntityRegistry &registry = GetOSEntityRegistry();

        UUID::SeedRNG(0x5EED4A4D00000005ULL);
        auto entity = dynamic_new<CharacterDeviceEntity>(true, "cross-name", "cross-alias");
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(entity)));

        //  The name is not registered as an alias and the alias is not a name.
        CheckFailedWithCode(OSEntityRegistryResultCodes::NO_SUCH_ENTITY,
                               registry.GetEntityByAlias<CharacterDeviceEntity>("cross-name"));
        CheckFailedWithCode(OSEntityRegistryResultCodes::NO_SUCH_ENTITY,
                               registry.GetEntityByName<CharacterDeviceEntity>("cross-alias"));
    }

    TEST(OSEntityGetNameAndAliasTests, UnknownNameAndAliasFailWithNoSuchEntity)
    {
        OSEntityRegistry &registry = GetOSEntityRegistry();

        CheckFailedWithCode(OSEntityRegistryResultCodes::NO_SUCH_ENTITY,
                               registry.GetEntityByName<CharacterDeviceEntity>("no-such-entity-name"));
        CheckFailedWithCode(OSEntityRegistryResultCodes::NO_SUCH_ENTITY,
                               registry.GetEntityByAlias<CharacterDeviceEntity>("no-such-entity-alias"));
    }

    // ============================================================================
    //  Remove Entity Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(OSEntityRemoveTests)
    {};
#pragma GCC diagnostic pop

    TEST(OSEntityRemoveTests, RemovingExistingEntitySucceeds)
    {
        OSEntityRegistry &registry = GetOSEntityRegistry();

        UUID::SeedRNG(0x5EED51D700000001ULL);
        auto entity = dynamic_new<CharacterDeviceEntity>(true, "remove-name", "remove-alias");
        UUID id = entity->Id();
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(entity)));

        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.RemoveEntityById(id)));
    }

    TEST(OSEntityRemoveTests, RemovedEntityIsNoLongerFindable)
    {
        OSEntityRegistry &registry = GetOSEntityRegistry();

        UUID::SeedRNG(0x5EED51D700000002ULL);
        auto entity = dynamic_new<CharacterDeviceEntity>(true, "removed-name", "removed-alias");
        UUID id = entity->Id();
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(entity)));
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.RemoveEntityById(id)));

        CHECK_FALSE(registry.DoesEntityExist(id));
        CheckFailedWithCode(OSEntityRegistryResultCodes::NO_SUCH_ENTITY,
                               registry.GetEntityById(id));
        CheckFailedWithCode(OSEntityRegistryResultCodes::NO_SUCH_ENTITY,
                               registry.GetEntityByName<CharacterDeviceEntity>("removed-name"));
        CheckFailedWithCode(OSEntityRegistryResultCodes::NO_SUCH_ENTITY,
                               registry.GetEntityByAlias<CharacterDeviceEntity>("removed-alias"));
    }

    TEST(OSEntityRemoveTests, FreesNameAndAliasForReuse)
    {
        OSEntityRegistry &registry = GetOSEntityRegistry();
UUID::SeedRNG(0x5EED51D700000003ULL);
        
        auto first = dynamic_new<CharacterDeviceEntity>(true, "reused-name", "reused-alias");
        UUID first_id = first->Id();
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(first)));
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.RemoveEntityById(first_id)));

        auto replacement = dynamic_new<CharacterDeviceEntity>(true, "reused-name", "reused-alias");
        UUID replacement_id = replacement->Id();
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(replacement)));

        CHECK_TRUE(registry.DoesEntityExist(replacement_id));
        CheckFailedWithCode(OSEntityRegistryResultCodes::NO_SUCH_ENTITY,
                               registry.GetEntityById(first_id));
    }

    TEST(OSEntityRemoveTests, RemovingUnknownEntityFailsWithNoSuchEntity)
    {
        OSEntityRegistry &registry = GetOSEntityRegistry();

        UUID::SeedRNG(0x5EED51D700000004ULL);
        UUID unknown = UUID::GenerateUUID(UUID::Versions::RANDOM);
        UUID::SeedRNG(0);  //  Restore the default RNG state.

        CheckCodeEquals(OSEntityRegistryResultCodes::NO_SUCH_ENTITY,
                        registry.RemoveEntityById(unknown));
    }

    // ============================================================================
    //  Find Entities By Type Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(OSEntityFindByTypeTests)
    {};
#pragma GCC diagnostic pop

    TEST(OSEntityFindByTypeTests, CollectsOnlyEntitiesOfRequestedType)
    {
        OSEntityRegistry &registry = GetOSEntityRegistry();

        auto character = dynamic_new<CharacterDeviceEntity>(true, "find-type-char", "find-type-char-alias");
        auto block = dynamic_new<BlockDeviceEntity>(true, "find-type-block", "find-type-block-alias");
        auto filesystem = dynamic_new<FilesystemEntity>(true, "find-type-fs", "find-type-fs-alias");
        UUID character_id = character->Id();
        UUID block_id = block->Id();
        UUID filesystem_id = filesystem->Id();

        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(character)));
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(block)));
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(filesystem)));

        //  minstd::list has no default constructor; nodes come from the dynamic
        //  heap resource defined in cpputest_main.cpp.
        using uuid_node_type = minstd::list<UUID>::node_type;
        minstd::pmr::polymorphic_allocator<uuid_node_type> list_alloc(&__os_dynamic_heap_resource);
        minstd::list<UUID> found(list_alloc);
        registry.FindEntitiesByType(OSEntityTypes::FILESYSTEM, found);

        size_t count = 0;
        bool found_filesystem = false;
        for (const auto &id : found)
        {
            ++count;
            if (id == filesystem_id)
            {
                found_filesystem = true;
            }
            else
            {
                CHECK_TRUE(id != character_id);
                CHECK_TRUE(id != block_id);
            }
        }

        CHECK_EQUAL(1, count);
        CHECK_TRUE(found_filesystem);
    }

    TEST(OSEntityFindByTypeTests, AppendsToCallerProvidedList)
    {
        OSEntityRegistry &registry = GetOSEntityRegistry();

        auto first = dynamic_new<CharacterDeviceEntity>(true, "append-type-a", "append-type-a-alias");
        auto second = dynamic_new<CharacterDeviceEntity>(true, "append-type-b", "append-type-b-alias");
        UUID first_id = first->Id();
        UUID second_id = second->Id();

        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(first)));
        CHECK_EQUAL(static_cast<int>(OSEntityRegistryResultCodes::SUCCESS),
                    static_cast<int>(registry.AddEntity(second)));

        using uuid_node_type = minstd::list<UUID>::node_type;
        minstd::pmr::polymorphic_allocator<uuid_node_type> list_alloc(&__os_dynamic_heap_resource);
        minstd::list<UUID> found(list_alloc);
        found.push_back(UUID::MAX);  //  FindEntitiesByType appends, it does not clear.

        registry.FindEntitiesByType(OSEntityTypes::CHARACTER_DEVICE, found);

        bool saw_max = false;
        bool saw_first = false;
        bool saw_second = false;
        size_t count = 0;
        for (const auto &id : found)
        {
            ++count;
            if (id == UUID::MAX)
            {
                saw_max = true;
            }
            else if (id == first_id)
            {
                saw_first = true;
            }
            else if (id == second_id)
            {
                saw_second = true;
            }
        }

        CHECK_TRUE(saw_max);
        CHECK_TRUE(saw_first);
        CHECK_TRUE(saw_second);
        CHECK_EQUAL(3, count);
    }

    // ============================================================================
    //  Result Code Helper Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(OSEntityResultCodeHelperTests)
    {};
#pragma GCC diagnostic pop

    TEST(OSEntityResultCodeHelperTests, SuccessfulAndFailedCoverAllCodes)
    {
        CHECK_TRUE(Successful(OSEntityRegistryResultCodes::SUCCESS));
        CHECK_FALSE(Failed(OSEntityRegistryResultCodes::SUCCESS));

        CHECK_FALSE(Successful(OSEntityRegistryResultCodes::NO_SUCH_ENTITY));
        CHECK_FALSE(Successful(OSEntityRegistryResultCodes::ENTITY_ID_ALREADY_IN_USE));
        CHECK_FALSE(Successful(OSEntityRegistryResultCodes::ENTITY_NAME_ALREADY_IN_USE));
        CHECK_FALSE(Successful(OSEntityRegistryResultCodes::ENTITY_ALIAS_ALREADY_IN_USE));
        CHECK_FALSE(Successful(OSEntityRegistryResultCodes::ERROR_SAVING_ENTITY_BY_UUID));

        CHECK_TRUE(Failed(OSEntityRegistryResultCodes::NO_SUCH_ENTITY));
        CHECK_TRUE(Failed(OSEntityRegistryResultCodes::ENTITY_ID_ALREADY_IN_USE));
        CHECK_TRUE(Failed(OSEntityRegistryResultCodes::ENTITY_NAME_ALREADY_IN_USE));
        CHECK_TRUE(Failed(OSEntityRegistryResultCodes::ENTITY_ALIAS_ALREADY_IN_USE));
        CHECK_TRUE(Failed(OSEntityRegistryResultCodes::ERROR_SAVING_ENTITY_BY_UUID));
    }
}
