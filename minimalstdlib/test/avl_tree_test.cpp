// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <avl_tree>

#include <dynamic_string>
#include <fixed_string>
#include <functional>

#include <heap_allocator>
#include <single_block_memory_heap>
#include <stack_allocator>

#include <memory>

#define TEST_BUFFER_SIZE 65536

#define CREATE_TEST_ELEMENT_UNIQUE_PTR(key, value) AVLTreeUniquePointer::value_type(key, minstd::move(minstd::unique_ptr<TestElement>(new (test_element_heap.allocate_block<TestElement>(1)) TestElement(value), test_element_heap)))

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (AVLTreeTests)
    {
    };
#pragma GCC diagnostic pop

    static char buffer[TEST_BUFFER_SIZE];
    static char buffer2[TEST_BUFFER_SIZE];

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
        MoveOnlyTestElement(MoveOnlyTestElement &) = delete;

        MoveOnlyTestElement(MoveOnlyTestElement &&) = default;

        MoveOnlyTestElement &operator=(MoveOnlyTestElement &) = delete;
        const MoveOnlyTestElement &operator=(const MoveOnlyTestElement &) = delete;

        MoveOnlyTestElement &operator=(MoveOnlyTestElement &&element_to_move)
        {
            value_ = element_to_move.value_;

            return *this;
        }

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

    using AVLTreeUniquePointer = minstd::avl_tree<uint32_t, minstd::unique_ptr<TestElement>>;

    using AVLTreeUniquePointerStaticHeapAllocator = minstd::heap_allocator<AVLTreeUniquePointer::node_type>;
    using AVLTreeUniquePointerStackAllocator = minstd::stack_allocator<AVLTreeUniquePointer::node_type, 24>;

    bool operator==(const minstd::reference_wrapper<minstd::dynamic_string<128>> &lhs, const minstd::string &rhs)
    {
        return lhs.get() == rhs;
    }

    using AVLTreeStringKey = minstd::avl_tree<minstd::reference_wrapper<minstd::dynamic_string<128>>, uint32_t>;

    using AVLTreeStringKeyAllocator = minstd::allocator<AVLTreeStringKey::node_type>;
    using AVLTreeStringKeyStaticHeapAllocator = minstd::heap_allocator<AVLTreeStringKey::node_type>;
    using AVLTreeStringKeyStackAllocator = minstd::stack_allocator<AVLTreeStringKey::node_type, 24>;

    void iterator_invariants(minstd::allocator<AVLTree::node_type> &allocator)
    {
        AVLTree tree(allocator);

        CHECK(tree.empty());
        CHECK_EQUAL(tree.size(), 0);

        CHECK(tree.begin() == tree.end());
        CHECK(++tree.begin() == tree.end());
        CHECK(++tree.end() == tree.end());
        CHECK(--tree.begin() == tree.end());
        CHECK(--tree.end() == tree.end());
    }

    void basic_iterator_tests(minstd::allocator<AVLTree::node_type> &allocator)
    {
        AVLTree tree(allocator);

        // constructing an AVL tree

        CHECK(tree.empty());
        CHECK_EQUAL(tree.size(), 0);

        CHECK(tree.insert(AVLTree::value_type(5, TestElement(15))).second());

        CHECK(!tree.empty());
        CHECK_EQUAL(tree.size(), 1);

        CHECK_EQUAL(tree.begin()->first(), 5);
        CHECK_EQUAL(tree.begin()->second().value(), 15);

        CHECK(++tree.begin() == tree.end());
        CHECK(--tree.end() == tree.begin());

        uint32_t count = 0;

        for (auto itr = tree.begin(); itr != tree.end(); itr++)
        {
            count++;
        }

        CHECK_EQUAL(count, 1);

        count = 0;

        for (auto itr = tree.end(); itr != tree.begin(); itr--)
        {
            count++;
        }

        CHECK_EQUAL(count, 1);
    }

    void basic_tests(minstd::allocator<AVLTree::node_type> &allocator)
    {
        AVLTree tree(allocator);

        // constructing an AVL tree

        CHECK(tree.empty());
        CHECK_EQUAL(tree.size(), 0);

        CHECK(tree.insert(AVLTree::value_type(5, TestElement(15))).second());

        CHECK(!tree.empty());
        CHECK_EQUAL(tree.size(), 1);

        CHECK_EQUAL(tree.insert(10, TestElement(110)).first()->first(), 10);
        CHECK(tree.insert(3, TestElement(13)).second());
        CHECK_EQUAL(tree.insert(AVLTree::value_type(24, TestElement(124))).first()->second().value(), 124);

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
        CHECK(tree.insert(AVLTree::value_type(6, TestElement(16))).second());

        CHECK_EQUAL(tree.size(), 20);

        //  Inserting an element with an existing key should fail and return an iterator to the existing element

        auto bad_insert_result = tree.insert(AVLTree::value_type(57, TestElement(257)));

        CHECK(!bad_insert_result.second());
        CHECK_EQUAL(bad_insert_result.first()->first(), 57);
        CHECK_EQUAL(bad_insert_result.first()->second().value(), 157);

        //  Test ordering and iteration both foward and reverse

        size_t count = 0;
        uint32_t last_key = 0;

        for (AVLTree::iterator itr = tree.begin(); itr != tree.end(); itr++)
        {
            CHECK(itr->first() > last_key);
            last_key = itr->first();
            count++;
        }

        CHECK_EQUAL(tree.size(), 20);
        CHECK_EQUAL(count, 20);

        count = 0;
        last_key = 100;

        for (AVLTree::iterator itr = --tree.end(); itr != tree.begin(); itr--)
        {
            CHECK(itr->first() < last_key);
            last_key = itr->first();
            count++;
        }

        //  Test delete twice

        CHECK(tree.begin()->first() < last_key);
        count++;

        CHECK_EQUAL(tree.size(), 20);
        CHECK_EQUAL(count, 20);

        uint32_t key_to_delete = 10;

        CHECK_EQUAL(tree.erase(key_to_delete), 1);

        count = 0;
        last_key = 0;

        for (AVLTree::iterator itr = tree.begin(); itr != tree.end(); itr++)
        {
            CHECK(itr->first() != key_to_delete);
            CHECK(itr->first() > last_key);
            last_key = itr->first();
            count++;
        }

        CHECK_EQUAL(count, 19);

        count = 0;
        last_key = 100;

        for (AVLTree::iterator itr = --tree.end(); itr != tree.begin(); itr--)
        {
            CHECK(itr->first() != key_to_delete);
            CHECK(itr->first() < last_key);
            last_key = itr->first();
            count++;
        }

        CHECK(tree.begin()->first() < last_key);
        count++;

        CHECK_EQUAL(tree.size(), 19);
        CHECK_EQUAL(count, 19);

        key_to_delete = 57;

        AVLTree::iterator itr_to_erase = tree.find(key_to_delete);

        CHECK_EQUAL(tree.erase(itr_to_erase)->first(), 64);

        count = 0;
        last_key = 0;

        for (AVLTree::iterator itr = tree.begin(); itr != tree.end(); itr++)
        {
            CHECK(itr->first() != key_to_delete);
            CHECK(itr->first() > last_key);
            last_key = itr->first();
            count++;
        }

        CHECK_EQUAL(tree.size(), 18);
        CHECK_EQUAL(count, 18);

        count = 0;
        last_key = 100;

        for (AVLTree::iterator itr = --tree.end(); itr != tree.begin(); itr--)
        {
            CHECK(itr->first() != key_to_delete);
            CHECK(itr->first() < last_key);
            last_key = itr->first();
            count++;
        }

        CHECK(tree.begin()->first() < last_key);
        count++;

        CHECK_EQUAL(count, 18);

        key_to_delete = 12;

        itr_to_erase = tree.find(key_to_delete);

        CHECK_EQUAL(tree.erase(itr_to_erase)->first(), 13);

        count = 0;
        last_key = 0;

        for (AVLTree::iterator itr = tree.begin(); itr != tree.end(); itr++)
        {
            CHECK(itr->first() != key_to_delete);
            CHECK(itr->first() > last_key);
            last_key = itr->first();
            count++;
        }

        CHECK_EQUAL(tree.size(), 17);
        CHECK_EQUAL(count, 17);

        count = 0;
        last_key = 100;

        for (AVLTree::iterator itr = --tree.end(); itr != tree.begin(); itr--)
        {
            CHECK(itr->first() != key_to_delete);
            CHECK(itr->first() < last_key);
            last_key = itr->first();
            count++;
        }

        CHECK(tree.begin()->first() < last_key);
        count++;

        CHECK_EQUAL(count, 17);

        //  Delete non existant keys

        CHECK_EQUAL(tree.erase(101), 0);
        CHECK_EQUAL(tree.erase(0), 0);
        CHECK_EQUAL(tree.erase(46), 0);
        CHECK(tree.erase(tree.end()) == tree.end());

        CHECK_EQUAL(tree.size(), 17);

        //  Test Find

        CHECK(tree.find(43) != tree.end());
        CHECK_EQUAL(tree.find(43)->second().value(), 143);
        CHECK(tree.find(44) == tree.end());

        CHECK(tree.find(6) != tree.end());
        CHECK_EQUAL(tree.find(6)->second().value(), 16);
        CHECK(tree.find(4) == tree.end());

        CHECK_EQUAL(tree.size(), 17);
    }

    void basic_tests_move_only(AVLTreeMoveOnlyAllocator &allocator)
    {
        AVLTreeMoveOnly tree(allocator);

        // constructing an AVL tree

        CHECK(tree.empty());
        CHECK_EQUAL(tree.size(), 0);

        CHECK(tree.insert(AVLTreeMoveOnly::value_type(5, MoveOnlyTestElement(15))).second());

        CHECK(!tree.empty());
        CHECK_EQUAL(tree.size(), 1);

        CHECK_EQUAL(tree.insert(10, MoveOnlyTestElement(110)).first()->first(), 10);
        CHECK(tree.insert(3, MoveOnlyTestElement(13)).second());
        CHECK_EQUAL(tree.insert(AVLTreeMoveOnly::value_type(24, MoveOnlyTestElement(124))).first()->second().value(), 124);

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

        CHECK(tree.insert(AVLTreeMoveOnly::value_type(6, MoveOnlyTestElement(16))).second());

        CHECK_EQUAL(tree.size(), 20);

        //  Inserting an element with an existing key should fail and return an iterator to the existing element

        auto bad_insert_result = tree.insert(AVLTreeMoveOnly::value_type(57, MoveOnlyTestElement(257)));

        CHECK(!bad_insert_result.second());
        CHECK_EQUAL(bad_insert_result.first()->first(), 57);
        CHECK_EQUAL(bad_insert_result.first()->second().value(), 157);

        //  Test ordering and iteration both foward and reverse

        size_t count = 0;
        uint32_t last_key = 0;

        for (AVLTreeMoveOnly::iterator itr = tree.begin(); itr != tree.end(); itr++)
        {
            CHECK(itr->first() > last_key);
            last_key = itr->first();
            count++;
        }

        CHECK_EQUAL(tree.size(), 20);
        CHECK_EQUAL(count, 20);

        count = 0;
        last_key = 100;

        for (AVLTreeMoveOnly::iterator itr = --tree.end(); itr != tree.begin(); itr--)
        {
            CHECK(itr->first() < last_key);
            last_key = itr->first();
            count++;
        }
    }

    void basic_tests_unique_pointer(minstd::allocator<AVLTreeUniquePointer::node_type> &allocator, minstd::single_block_memory_heap &test_element_heap)
    {
        AVLTreeUniquePointer tree(allocator);

        // constructing an AVL tree

        CHECK(tree.empty());
        CHECK_EQUAL(tree.size(), 0);

        CHECK(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(5, 15)).second());

        CHECK(!tree.empty());
        CHECK_EQUAL(tree.size(), 1);

        CHECK_EQUAL(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(10, 110)).first()->first(), 10);
        CHECK(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(3, 13)).second());
        CHECK_EQUAL(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(24, 124)).first()->second()->value(), 124);

        tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(51, 151));
        tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(17, 117));
        tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(12, 112));
        tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(13, 113));
        tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(43, 143));
        tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(22, 122));
        tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(96, 196));
        tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(64, 164));
        tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(2, 12));
        tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(33, 133));
        tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(57, 157));
        tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(9, 19));
        tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(68, 168));
        tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(73, 173));
        tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(81, 181));
        CHECK(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(6, 16)).second());

        CHECK_EQUAL(tree.size(), 20);

        //  Inserting an element with an existing key should fail and return an iterator to the existing element

        auto bad_insert_result = tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(57, 257));

        CHECK(!bad_insert_result.second());
        CHECK_EQUAL(bad_insert_result.first()->first(), 57);
        CHECK_EQUAL(bad_insert_result.first()->second()->value(), 157);

        //  Test ordering and iteration both foward and reverse

        size_t count = 0;
        uint32_t last_key = 0;

        for (auto itr = tree.begin(); itr != tree.end(); itr++)
        {
            CHECK(itr->first() > last_key);
            last_key = itr->first();
            count++;
        }

        CHECK_EQUAL(tree.size(), 20);
        CHECK_EQUAL(count, 20);

        count = 0;
        last_key = 100;

        for (auto itr = --tree.end(); itr != tree.begin(); itr--)
        {
            CHECK(itr->first() < last_key);
            last_key = itr->first();
            count++;
        }

        //  Test delete twice

        CHECK(tree.begin()->first() < last_key);
        count++;

        CHECK_EQUAL(tree.size(), 20);
        CHECK_EQUAL(count, 20);

        uint32_t key_to_delete = 10;

        CHECK_EQUAL(tree.erase(key_to_delete), 1);

        count = 0;
        last_key = 0;

        for (auto itr = tree.begin(); itr != tree.end(); itr++)
        {
            CHECK(itr->first() != key_to_delete);
            CHECK(itr->first() > last_key);
            last_key = itr->first();
            count++;
        }

        CHECK_EQUAL(count, 19);

        count = 0;
        last_key = 100;

        for (auto itr = --tree.end(); itr != tree.begin(); itr--)
        {
            CHECK(itr->first() != key_to_delete);
            CHECK(itr->first() < last_key);
            last_key = itr->first();
            count++;
        }

        CHECK(tree.begin()->first() < last_key);
        count++;

        CHECK_EQUAL(tree.size(), 19);
        CHECK_EQUAL(count, 19);

        key_to_delete = 57;

        auto itr_to_erase = tree.find(key_to_delete);

        CHECK_EQUAL(tree.erase(itr_to_erase)->first(), 64);

        count = 0;
        last_key = 0;

        for (auto itr = tree.begin(); itr != tree.end(); itr++)
        {
            CHECK(itr->first() != key_to_delete);
            CHECK(itr->first() > last_key);
            last_key = itr->first();
            count++;
        }

        CHECK_EQUAL(tree.size(), 18);
        CHECK_EQUAL(count, 18);

        count = 0;
        last_key = 100;

        for (auto itr = --tree.end(); itr != tree.begin(); itr--)
        {
            CHECK(itr->first() != key_to_delete);
            CHECK(itr->first() < last_key);
            last_key = itr->first();
            count++;
        }

        CHECK(tree.begin()->first() < last_key);
        count++;

        CHECK_EQUAL(count, 18);

        key_to_delete = 12;

        itr_to_erase = tree.find(key_to_delete);

        CHECK_EQUAL(tree.erase(itr_to_erase)->first(), 13);

        count = 0;
        last_key = 0;

        for (auto itr = tree.begin(); itr != tree.end(); itr++)
        {
            CHECK(itr->first() != key_to_delete);
            CHECK(itr->first() > last_key);
            last_key = itr->first();
            count++;
        }

        CHECK_EQUAL(tree.size(), 17);
        CHECK_EQUAL(count, 17);

        count = 0;
        last_key = 100;

        for (auto itr = --tree.end(); itr != tree.begin(); itr--)
        {
            CHECK(itr->first() != key_to_delete);
            CHECK(itr->first() < last_key);
            last_key = itr->first();
            count++;
        }

        CHECK(tree.begin()->first() < last_key);
        count++;

        CHECK_EQUAL(count, 17);

        //  Delete non existant keys

        CHECK_EQUAL(tree.erase(101), 0);
        CHECK_EQUAL(tree.erase(0), 0);
        CHECK_EQUAL(tree.erase(46), 0);
        CHECK(tree.erase(tree.end()) == tree.end());

        CHECK_EQUAL(tree.size(), 17);

        //  Test Find

        CHECK(tree.find(43) != tree.end());
        CHECK_EQUAL(tree.find(43)->second()->value(), 143);
        CHECK(tree.find(44) == tree.end());

        CHECK(tree.find(6) != tree.end());
        CHECK_EQUAL(tree.find(6)->second()->value(), 16);
        CHECK(tree.find(4) == tree.end());

        CHECK_EQUAL(tree.size(), 17);
    }

    TEST(AVLTreeTests, TestAVLTreeIteratorInvariants)
    {
        minstd::single_block_memory_heap test_heap(buffer, 4096);
        AVLTreeStaticHeapAllocator heap_allocator(test_heap);

        iterator_invariants(heap_allocator);
        basic_iterator_tests(heap_allocator);

        AVLTreeStackAllocator stack_allocator;

        iterator_invariants(stack_allocator);
        basic_iterator_tests(stack_allocator);
    }

    TEST(AVLTreeTests, TestAVLTreeBasicOperations)
    {
        minstd::single_block_memory_heap test_heap(buffer, 4096);
        AVLTreeStaticHeapAllocator heap_allocator(test_heap);

        basic_tests(heap_allocator);

        AVLTreeStackAllocator stack_allocator;

        basic_tests(stack_allocator);
    }

    TEST(AVLTreeTests, TestAVLTreeBasicOperationsWithMoveOnlyValueType)
    {
        MoveOnlyTestElement element1 = MoveOnlyTestElement(7654);
        MoveOnlyTestElement element2 = MoveOnlyTestElement(minstd::move(element1));

        CHECK_EQUAL(element2.value(), 7654);

        minstd::single_block_memory_heap test_heap(buffer, 4096);
        AVLTreeMoveOnlyStaticHeapAllocator heap_allocator(test_heap);

        basic_tests_move_only(heap_allocator);

        AVLTreeMoveOnlyStackAllocator stack_allocator;

        basic_tests_move_only(stack_allocator);
    }

    TEST(AVLTreeTests, TestAVLTreeWithUniquePointerTypeForLeaks)
    {
        minstd::single_block_memory_heap test_element_heap(buffer2, 4096);

        CHECK_EQUAL(0, test_element_heap.bytes_in_use());

        minstd::single_block_memory_heap test_heap(buffer, 4096);
        AVLTreeUniquePointerStaticHeapAllocator heap_allocator(test_heap);

        // constructing an AVL tree
        {
            AVLTreeUniquePointer tree(heap_allocator);

            CHECK(tree.empty());
            CHECK_EQUAL(tree.size(), 0);

            CHECK(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(1, 11)).second());
            CHECK(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(2, 12)).second());
            CHECK(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(3, 13)).second());
            CHECK(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(4, 14)).second());
            CHECK(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(5, 15)).second());
            CHECK(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(6, 16)).second());

            tree.erase(5);
            tree.erase(3);

            CHECK(!tree.empty());
            CHECK_EQUAL(tree.size(), 4);
        }

        CHECK_EQUAL(0, test_element_heap.bytes_in_use());

        //  Check the clear operation

        {
            AVLTreeUniquePointer tree(heap_allocator);

            CHECK(tree.empty());
            CHECK_EQUAL(tree.size(), 0);

            CHECK(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(1, 11)).second());
            CHECK(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(2, 12)).second());
            CHECK(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(3, 13)).second());

            tree.clear();

            CHECK(tree.empty());
            CHECK_EQUAL(tree.size(), 0);
        }

        CHECK_EQUAL(0, test_element_heap.bytes_in_use());
    }

    TEST(AVLTreeTests, TestAVLTreeBasicOperationsWithUniquePointerType)
    {
        minstd::single_block_memory_heap test_element_heap(buffer2, 4096);

        CHECK_EQUAL(0, test_element_heap.bytes_in_use());

        minstd::single_block_memory_heap test_heap(buffer, 4096);
        AVLTreeUniquePointerStaticHeapAllocator heap_allocator(test_heap);

        basic_tests_unique_pointer(heap_allocator, test_element_heap);

        AVLTreeStackAllocator stack_allocator;

        basic_tests_unique_pointer(heap_allocator, test_element_heap);

        CHECK_EQUAL(0, test_element_heap.bytes_in_use());
    }

    TEST(AVLTreeTests, TestAVLTreeWithStringKeyOperations)
    {
        minstd::single_block_memory_heap test_heap(buffer, 4096);
        AVLTreeStringKeyStaticHeapAllocator heap_allocator(test_heap);

        minstd::single_block_memory_heap string_test_heap(buffer2, 4096);
        minstd::heap_allocator<char> string_allocator(string_test_heap);

        CHECK_EQUAL(0, test_heap.bytes_in_use());
        CHECK_EQUAL(0, string_test_heap.bytes_in_use());

        {
            AVLTreeStringKey tree(heap_allocator);

            // constructing an AVL tree

            CHECK(tree.empty());
            CHECK_EQUAL(tree.size(), 0);

            auto key_5 = minstd::dynamic_string<128>("five", string_allocator);
            auto key_2 = minstd::dynamic_string<128>("two", string_allocator);
            auto key_7 = minstd::dynamic_string<128>("seven", string_allocator);
            auto key_1 = minstd::dynamic_string<128>("one", string_allocator);

            CHECK(tree.insert(minstd::reference_wrapper<minstd::dynamic_string<128>>(minstd::move(key_5)), 5).second());

            CHECK(!tree.empty());
            CHECK_EQUAL(tree.size(), 1);

            auto two_string = minstd::fixed_string<128>("two");

            CHECK(tree.insert(minstd::reference_wrapper(minstd::move(key_2)), 2).first()->first() == static_cast<const minstd::string &>(two_string));
            CHECK(tree.insert(minstd::reference_wrapper(minstd::move(key_7)), 7).second());
            CHECK(tree.insert(minstd::reference_wrapper(minstd::move(key_1)), 1).second());

            auto itr = tree.begin();

            //  Ordering is by the string key

            CHECK_EQUAL(itr->second(), 5);
            itr++;
            CHECK_EQUAL(itr->second(), 1);
            itr++;
            CHECK_EQUAL(itr->second(), 7);
            itr++;
            CHECK_EQUAL(itr->second(), 2);
            itr++;
            CHECK(itr == tree.end());
        }

        CHECK_EQUAL(0, test_heap.bytes_in_use());
        CHECK_EQUAL(0, string_test_heap.bytes_in_use());
    }
}
