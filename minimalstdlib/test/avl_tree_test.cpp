// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <catch2/catch_test_macros.hpp>

#include "../include/minstdconfig.h"

#include "../include/avl_tree"

#include "../include/heap_allocator"
#include "../include/stack_allocator"
#include "../include/single_block_memory_heap"


#define TEST_BUFFER_SIZE 65536

namespace
{
    static char buffer[TEST_BUFFER_SIZE];

    class TestElement
    {
    public:
        explicit TestElement(uint32_t value)
            : value_(value)
        {
        }

        TestElement(const TestElement &) = default;

        uint32_t value() const
        {
            return value_;
        }

        bool operator<(const TestElement &other) const
        {
            return value_ < other.value_;
        }

    private:
        uint32_t value_ = 0;
        char empty_space_[18];
    };

    class MoveOnlyTestElement
    {
    public:

        MoveOnlyTestElement() = delete;

        explicit MoveOnlyTestElement(uint32_t value)
            : value_(value)
        {
        }

        MoveOnlyTestElement(const MoveOnlyTestElement &) = delete;
        MoveOnlyTestElement( MoveOnlyTestElement &) = delete;

        MoveOnlyTestElement( MoveOnlyTestElement && ) = default;

        MoveOnlyTestElement operator=( MoveOnlyTestElement &) = delete;
        const MoveOnlyTestElement operator=( const MoveOnlyTestElement &) = delete;


        uint32_t value() const
        {
            return value_;
        }

        bool operator<(const MoveOnlyTestElement &other) const
        {
            return value_ < other.value_;
        }

    private:
        uint32_t value_ = 0;
        char empty_space_[18];
    };

    using AVLTree = minstd::avl_tree<uint32_t, TestElement>;

    using AVLTreeAllocator = minstd::allocator<AVLTree::node_type>;
    using AVLTreeStaticHeapAllocator = minstd::heap_allocator<AVLTree::node_type>;
    using AVLTreeStackAllocator = minstd::stack_allocator<AVLTree::node_type, 24>;

    using AVLTreeMoveOnly = minstd::avl_tree<uint32_t, MoveOnlyTestElement>;

    using AVLTreeMoveOnlyAllocator = minstd::allocator<AVLTreeMoveOnly::node_type>;
    using AVLTreeMoveOnlyStaticHeapAllocator = minstd::heap_allocator<AVLTreeMoveOnly::node_type>;
    using AVLTreeMoveOnlyStackAllocator = minstd::stack_allocator<AVLTreeMoveOnly::node_type, 24>;

    void iterator_invariants(minstd::allocator<AVLTree::node_type>& allocator)
    {
        AVLTree tree(allocator);

        REQUIRE( tree.empty() );
        REQUIRE( tree.size() == 0 );
        
        REQUIRE( tree.begin() == tree.end() );
        REQUIRE( ++tree.begin() == tree.end() );
        REQUIRE( ++tree.end() == tree.end() );
        REQUIRE( --tree.begin() == tree.end() );
        REQUIRE( --tree.end() == tree.end() );
    }

    
    void basic_iterator_tests(minstd::allocator<AVLTree::node_type>& allocator)
    {
        AVLTree tree(allocator);

        // constructing an AVL tree

        REQUIRE( tree.empty() );
        REQUIRE( tree.size() == 0 );

        REQUIRE( tree.insert(AVLTree::value_type(5, TestElement(15))).second() );

        REQUIRE( !tree.empty() );
        REQUIRE( tree.size() == 1 );

        REQUIRE( tree.begin()->first() == 5 );
        REQUIRE( tree.begin()->second().value() == 15 );

        REQUIRE( ++tree.begin() == tree.end() );
        REQUIRE( --tree.end() == tree.begin() );

        uint32_t count = 0;

        for( auto itr = tree.begin(); itr != tree.end(); itr++ )
        {
            count++;
        }

        REQUIRE( count == 1 );

        count = 0;

        for( auto itr = tree.end(); itr != tree.begin(); itr-- )
        {
            count++;
        }

        REQUIRE( count == 1 );
    }

    void basic_tests(minstd::allocator<AVLTree::node_type>& allocator)
    {
        AVLTree tree(allocator);

        // constructing an AVL tree

        REQUIRE( tree.empty() );
        REQUIRE( tree.size() == 0 );

        REQUIRE( tree.insert(AVLTree::value_type(5, TestElement(15))).second() );

        REQUIRE( !tree.empty() );
        REQUIRE( tree.size() == 1 );

        REQUIRE( tree.insert(10, TestElement(110)).first()->first() == 10 );
        REQUIRE( tree.insert(3, TestElement(13)).second() );
        REQUIRE( tree.insert(AVLTree::value_type(24, TestElement(124))).first()->second().value() == 124 );
        
        tree.insert(51, TestElement(151));
        tree.insert(AVLTree::value_type(17, TestElement(117)));
        tree.insert(12, TestElement(112));
        tree.insert(AVLTree::value_type(13, TestElement(113)));
        tree.insert(43, TestElement(143));
        tree.insert(AVLTree::value_type(22, TestElement(122)));
        tree.insert(96, TestElement(196));
        tree.insert(AVLTree::value_type(64, TestElement(164)));
        tree.insert(2, TestElement(12));
        tree.insert(AVLTree::value_type(33, TestElement(133)));
        tree.insert(AVLTree::value_type(57, TestElement(157)));
        tree.insert(AVLTree::value_type(9, TestElement(19)));
        tree.insert(AVLTree::value_type(68, TestElement(168)));
        tree.insert(AVLTree::value_type(73, TestElement(173)));
        tree.insert(AVLTree::value_type(81, TestElement(181)));
        REQUIRE( tree.insert(AVLTree::value_type(6, TestElement(16))).second() );

        REQUIRE( tree.size() == 20 );

        //  Inserting an element with an existing key should fail and return an iterator to the existing element

        auto bad_insert_result = tree.insert(AVLTree::value_type(57, TestElement(257)));

        REQUIRE( !bad_insert_result.second() );
        REQUIRE( bad_insert_result.first()->first() == 57 );
        REQUIRE( bad_insert_result.first()->second().value() == 157 );

        //  Test ordering and iteration both foward and reverse

        size_t count = 0;
        uint32_t last_key = 0;

        for (AVLTree::iterator itr = tree.begin(); itr != tree.end(); itr++)
        {
            REQUIRE(itr->first() > last_key);
            last_key = itr->first();
            count++;
        }

        REQUIRE( tree.size() == 20 );
        REQUIRE(count == 20);

        count = 0;
        last_key = 100;

        for (AVLTree::iterator itr = --tree.end(); itr != tree.begin(); itr--)
        {
            REQUIRE(itr->first() < last_key);
            last_key = itr->first();
            count++;
        }

        //  Test delete twice

        REQUIRE(tree.begin()->first() < last_key);
        count++;

        REQUIRE( tree.size() == 20 );
        REQUIRE(count == 20);

        uint32_t key_to_delete = 10;

        REQUIRE( tree.erase(key_to_delete) == 1 );

        count = 0;
        last_key = 0;

        for (AVLTree::iterator itr = tree.begin(); itr != tree.end(); itr++)
        {
            REQUIRE(itr->first() != key_to_delete);
            REQUIRE(itr->first() > last_key);
            last_key = itr->first();
            count++;
        }

        REQUIRE(count == 19);

        count = 0;
        last_key = 100;

        for (AVLTree::iterator itr = --tree.end(); itr != tree.begin(); itr--)
        {
            REQUIRE(itr->first() != key_to_delete);
            REQUIRE(itr->first() < last_key);
            last_key = itr->first();
            count++;
        }

        REQUIRE(tree.begin()->first() < last_key);
        count++;

        REQUIRE( tree.size() == 19 );
        REQUIRE(count == 19);

        key_to_delete = 57;

        AVLTree::iterator itr_to_erase = tree.find( key_to_delete );

        REQUIRE( tree.erase(itr_to_erase)->first() == 64 );

        count = 0;
        last_key = 0;

        for (AVLTree::iterator itr = tree.begin(); itr != tree.end(); itr++)
        {
            REQUIRE(itr->first() != key_to_delete);
            REQUIRE(itr->first() > last_key);
            last_key = itr->first();
            count++;
        }

        REQUIRE( tree.size() == 18 );
        REQUIRE(count == 18);

        count = 0;
        last_key = 100;

        for (AVLTree::iterator itr = --tree.end(); itr != tree.begin(); itr--)
        {
            REQUIRE(itr->first() != key_to_delete);
            REQUIRE(itr->first() < last_key);
            last_key = itr->first();
            count++;
        }

        REQUIRE(tree.begin()->first() < last_key);
        count++;

        REQUIRE(count == 18);

        key_to_delete = 12;

        itr_to_erase = tree.find( key_to_delete );

        REQUIRE( tree.erase(itr_to_erase)->first() == 13 );

        count = 0;
        last_key = 0;

        for (AVLTree::iterator itr = tree.begin(); itr != tree.end(); itr++)
        {
            REQUIRE(itr->first() != key_to_delete);
            REQUIRE(itr->first() > last_key);
            last_key = itr->first();
            count++;
        }

        REQUIRE( tree.size() == 17 );
        REQUIRE(count == 17);

        count = 0;
        last_key = 100;

        for (AVLTree::iterator itr = --tree.end(); itr != tree.begin(); itr--)
        {
            REQUIRE(itr->first() != key_to_delete);
            REQUIRE(itr->first() < last_key);
            last_key = itr->first();
            count++;
        }

        REQUIRE(tree.begin()->first() < last_key);
        count++;

        REQUIRE(count == 17);

        //  Delete non existant keys

        REQUIRE( tree.erase(101) == 0 );
        REQUIRE( tree.erase(0) == 0 );
        REQUIRE( tree.erase(46) == 0 );
        REQUIRE( tree.erase(tree.end()) == tree.end() );

        REQUIRE( tree.size() == 17 );

        //  Test Find

        REQUIRE(tree.find(43) != tree.end());
        REQUIRE(tree.find(43)->second().value() == 143);
        REQUIRE(tree.find(44) == tree.end());

        REQUIRE(tree.find(6) != tree.end());
        REQUIRE(tree.find(6)->second().value() == 16);
        REQUIRE(tree.find(4) == tree.end());

        REQUIRE( tree.size() == 17 );
    }

    void basic_tests_move_only(AVLTreeMoveOnlyAllocator& allocator)
    {
        AVLTreeMoveOnly tree(allocator);

        // constructing an AVL tree

        REQUIRE( tree.empty() );
        REQUIRE( tree.size() == 0 );

        REQUIRE( tree.insert(AVLTreeMoveOnly::value_type(5, MoveOnlyTestElement(15))).second() );

        REQUIRE( !tree.empty() );
        REQUIRE( tree.size() == 1 );

        REQUIRE( tree.insert(10, MoveOnlyTestElement(110)).first()->first() == 10 );
        REQUIRE( tree.insert(3, MoveOnlyTestElement(13)).second() );
        REQUIRE( tree.insert(AVLTreeMoveOnly::value_type(24, MoveOnlyTestElement(124))).first()->second().value() == 124 );
        
        tree.insert(51, MoveOnlyTestElement(151));
        tree.insert(AVLTreeMoveOnly::value_type(17, MoveOnlyTestElement(117)));
        tree.insert(12, MoveOnlyTestElement(112));
        tree.insert(AVLTreeMoveOnly::value_type(13, MoveOnlyTestElement(113)));
        tree.insert(43, MoveOnlyTestElement(143));
        tree.insert(AVLTreeMoveOnly::value_type(22, MoveOnlyTestElement(122)));
        tree.insert(96, MoveOnlyTestElement(196));
        tree.insert(AVLTreeMoveOnly::value_type(64, MoveOnlyTestElement(164)));
        tree.insert(2, MoveOnlyTestElement(12));
        tree.insert(AVLTreeMoveOnly::value_type(33, MoveOnlyTestElement(133)));
        tree.insert(AVLTreeMoveOnly::value_type(57, MoveOnlyTestElement(157)));
        tree.insert(AVLTreeMoveOnly::value_type(9, MoveOnlyTestElement(19)));
        tree.insert(AVLTreeMoveOnly::value_type(68, MoveOnlyTestElement(168)));
        tree.insert(AVLTreeMoveOnly::value_type(73, MoveOnlyTestElement(173)));
        tree.insert(AVLTreeMoveOnly::value_type(81, MoveOnlyTestElement(181)));
        REQUIRE( tree.insert(AVLTreeMoveOnly::value_type(6, MoveOnlyTestElement(16))).second() );

        REQUIRE( tree.size() == 20 );

        //  Inserting an element with an existing key should fail and return an iterator to the existing element

        auto bad_insert_result = tree.insert(AVLTreeMoveOnly::value_type(57, MoveOnlyTestElement(257)));

        REQUIRE( !bad_insert_result.second() );
        REQUIRE( bad_insert_result.first()->first() == 57 );
        REQUIRE( bad_insert_result.first()->second().value() == 157 );

        //  Test ordering and iteration both foward and reverse

        size_t count = 0;
        uint32_t last_key = 0;

        for (AVLTreeMoveOnly::iterator itr = tree.begin(); itr != tree.end(); itr++)
        {
            REQUIRE(itr->first() > last_key);
            last_key = itr->first();
            count++;
        }

        REQUIRE( tree.size() == 20 );
        REQUIRE(count == 20);

        count = 0;
        last_key = 100;

        for (AVLTreeMoveOnly::iterator itr = --tree.end(); itr != tree.begin(); itr--)
        {
            REQUIRE(itr->first() < last_key);
            last_key = itr->first();
            count++;
        }
    }


    TEST_CASE("Test AVL tree iterator invariants", "")
    {
        minstd::single_block_memory_heap test_heap(buffer, 4096);
        AVLTreeStaticHeapAllocator heap_allocator(test_heap);

        iterator_invariants(heap_allocator);
        basic_iterator_tests(heap_allocator);

        AVLTreeStackAllocator stack_allocator;

        iterator_invariants(stack_allocator);
        basic_iterator_tests(stack_allocator);
    }

    TEST_CASE("Test AVL tree basic operations", "")
    {
        minstd::single_block_memory_heap test_heap(buffer, 4096);
        AVLTreeStaticHeapAllocator heap_allocator(test_heap);

        basic_tests(heap_allocator);

        AVLTreeStackAllocator stack_allocator;

        basic_tests(stack_allocator);
    }

    TEST_CASE("Test AVL tree basic operations with move only value type", "")
    {
        MoveOnlyTestElement element1 = MoveOnlyTestElement(7654);
        MoveOnlyTestElement element2 = MoveOnlyTestElement( std::move(element1) );

        REQUIRE( element2.value() == 7654 );

        minstd::single_block_memory_heap test_heap(buffer, 4096);
        AVLTreeMoveOnlyStaticHeapAllocator heap_allocator(test_heap);

        basic_tests_move_only(heap_allocator);

        AVLTreeMoveOnlyStackAllocator stack_allocator;

        basic_tests_move_only(stack_allocator);
    }
}
