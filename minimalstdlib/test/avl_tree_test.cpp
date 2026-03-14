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

    class test_element
    {
    public:
        explicit test_element(uint32_t value)
            : value_(value)
        {
        }

        test_element(const test_element &) = default;

        uint32_t value() const
        {
            return value_;
        }

        bool operator<(const test_element &other) const
        {
            return value_ < other.value_;
        }

    private:
        uint32_t value_ = 0;
        char empty_space_[18];
    };

    class move_only_test_element
    {
    public:
        move_only_test_element() = delete;

        explicit move_only_test_element(uint32_t value)
            : value_(value)
        {
        }

        move_only_test_element(const move_only_test_element &) = delete;
        move_only_test_element(move_only_test_element &) = delete;

        move_only_test_element(move_only_test_element &&) = default;

        move_only_test_element &operator=(move_only_test_element &) = delete;
        const move_only_test_element &operator=(const move_only_test_element &) = delete;

        move_only_test_element &operator=(move_only_test_element &&element_to_move)
        {
            value_ = element_to_move.value_;

            return *this;
        }

        uint32_t value() const
        {
            return value_;
        }

        bool operator<(const move_only_test_element &other) const
        {
            return value_ < other.value_;
        }

    private:
        uint32_t value_ = 0;
        char empty_space_[18];
    };

    using avl_tree = minstd::avl_tree<uint32_t, test_element>;

    using AVLTreeAllocator = minstd::allocator<AVLTree::node_type>;
    using AVLTreeStaticHeapAllocator = minstd::heap_allocator<AVLTree::node_type>;
    using AVLTreeStackAllocator = minstd::stack_allocator<AVLTree::node_type, 24>;

    using avl_tree_move_only = minstd::avl_tree<uint32_t, move_only_test_element>;

    using AVLTreeMoveOnlyAllocator = minstd::allocator<AVLTreeMoveOnly::node_type>;
    using AVLTreeMoveOnlyStaticHeapAllocator = minstd::heap_allocator<AVLTreeMoveOnly::node_type>;
    using AVLTreeMoveOnlyStackAllocator = minstd::stack_allocator<AVLTreeMoveOnly::node_type, 24>;

    using avl_tree_unique_pointer = minstd::avl_tree<uint32_t, minstd::unique_ptr<test_element>>;

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
        avl_tree tree(allocator);

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
        avl_tree tree(allocator);

        // constructing an AVL tree

        CHECK(tree.empty());
        CHECK_EQUAL(tree.size(), 0);

        CHECK(get<1>(tree.insert(AVLTree::value_type(5, TestElement(15)))));

        CHECK(!tree.empty());
        CHECK_EQUAL(tree.size(), 1);

        CHECK_EQUAL(get<0>(*tree.begin()), 5);
        CHECK_EQUAL(get<1>(*tree.begin()).value(), 15);

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
        avl_tree tree(allocator);

        // constructing an AVL tree

        CHECK(tree.empty());
        CHECK_EQUAL(tree.size(), 0);

        CHECK(get<1>(tree.insert(AVLTree::value_type(5, TestElement(15)))));

        CHECK(!tree.empty());
        CHECK_EQUAL(tree.size(), 1);

        CHECK_EQUAL(get<0>(*get<0>(tree.insert(10, test_element(110)))), 10);
        CHECK(get<1>(tree.insert(3, test_element(13))));
        CHECK_EQUAL(get<1>(*get<0>(tree.insert(AVLTree::value_type(24, TestElement(124))))).value(), 124);

        tree.insert(51, test_element(151));
        tree.insert(AVLTree::value_type(17, TestElement(117)));
        tree.insert(12, test_element(112));
        tree.insert(AVLTree::value_type(13, TestElement(113)));
        tree.insert(43, test_element(143));
        tree.insert(AVLTree::value_type(22, TestElement(122)));
        tree.insert(96, test_element(196));
        tree.insert(AVLTree::value_type(64, TestElement(164)));
        tree.insert(2, test_element(12));
        tree.insert(AVLTree::value_type(33, TestElement(133)));
        tree.insert(AVLTree::value_type(57, TestElement(157)));
        tree.insert(AVLTree::value_type(9, TestElement(19)));
        tree.insert(AVLTree::value_type(68, TestElement(168)));
        tree.insert(AVLTree::value_type(73, TestElement(173)));
        tree.insert(AVLTree::value_type(81, TestElement(181)));
        CHECK(get<1>(tree.insert(AVLTree::value_type(6, TestElement(16)))));

        CHECK_EQUAL(tree.size(), 20);

        //  Inserting an element with an existing key should fail and return an iterator to the existing element

        auto bad_insert_result = tree.insert(AVLTree::value_type(57, TestElement(257)));

        CHECK(!get<1>(bad_insert_result));
        CHECK_EQUAL(get<0>(*get<0>(bad_insert_result)), 57);
        CHECK_EQUAL(get<1>(*get<0>(bad_insert_result)).value(), 157);

        //  Test ordering and iteration both foward and reverse

        size_t count = 0;
        uint32_t last_key = 0;

        for (AVLTree::iterator itr = tree.begin(); itr != tree.end(); itr++)
        {
            CHECK(get<0>(*itr) > last_key);
            last_key = get<0>(*itr);
            count++;
        }

        CHECK_EQUAL(tree.size(), 20);
        CHECK_EQUAL(count, 20);

        count = 0;
        last_key = 100;

        for (AVLTree::iterator itr = --tree.end(); itr != tree.begin(); itr--)
        {
            CHECK(get<0>(*itr) < last_key);
            last_key = get<0>(*itr);
            count++;
        }

        //  Test delete twice

        CHECK(get<0>(*tree.begin()) < last_key);
        count++;

        CHECK_EQUAL(tree.size(), 20);
        CHECK_EQUAL(count, 20);

        uint32_t key_to_delete = 10;

        CHECK_EQUAL(tree.erase(key_to_delete), 1);

        count = 0;
        last_key = 0;

        for (AVLTree::iterator itr = tree.begin(); itr != tree.end(); itr++)
        {
            CHECK(get<0>(*itr) != key_to_delete);
            CHECK(get<0>(*itr) > last_key);
            last_key = get<0>(*itr);
            count++;
        }

        CHECK_EQUAL(count, 19);

        count = 0;
        last_key = 100;

        for (AVLTree::iterator itr = --tree.end(); itr != tree.begin(); itr--)
        {
            CHECK(get<0>(*itr) != key_to_delete);
            CHECK(get<0>(*itr) < last_key);
            last_key = get<0>(*itr);
            count++;
        }

        CHECK(get<0>(*tree.begin()) < last_key);
        count++;

        CHECK_EQUAL(tree.size(), 19);
        CHECK_EQUAL(count, 19);

        key_to_delete = 57;

        AVLTree::iterator itr_to_erase = tree.find(key_to_delete);

        CHECK_EQUAL(get<0>(*tree.erase(itr_to_erase)), 64);

        count = 0;
        last_key = 0;

        for (AVLTree::iterator itr = tree.begin(); itr != tree.end(); itr++)
        {
            CHECK(get<0>(*itr) != key_to_delete);
            CHECK(get<0>(*itr) > last_key);
            last_key = get<0>(*itr);
            count++;
        }

        CHECK_EQUAL(tree.size(), 18);
        CHECK_EQUAL(count, 18);

        count = 0;
        last_key = 100;

        for (AVLTree::iterator itr = --tree.end(); itr != tree.begin(); itr--)
        {
            CHECK(get<0>(*itr) != key_to_delete);
            CHECK(get<0>(*itr) < last_key);
            last_key = get<0>(*itr);
            count++;
        }

        CHECK(get<0>(*tree.begin()) < last_key);
        count++;

        CHECK_EQUAL(count, 18);

        key_to_delete = 12;

        itr_to_erase = tree.find(key_to_delete);

        CHECK_EQUAL(get<0>(*tree.erase(itr_to_erase)), 13);

        count = 0;
        last_key = 0;

        for (AVLTree::iterator itr = tree.begin(); itr != tree.end(); itr++)
        {
            CHECK(get<0>(*itr) != key_to_delete);
            CHECK(get<0>(*itr) > last_key);
            last_key = get<0>(*itr);
            count++;
        }

        CHECK_EQUAL(tree.size(), 17);
        CHECK_EQUAL(count, 17);

        count = 0;
        last_key = 100;

        for (AVLTree::iterator itr = --tree.end(); itr != tree.begin(); itr--)
        {
            CHECK(get<0>(*itr) != key_to_delete);
            CHECK(get<0>(*itr) < last_key);
            last_key = get<0>(*itr);
            count++;
        }

        CHECK(get<0>(*tree.begin()) < last_key);
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
        CHECK_EQUAL(get<1>(*tree.find(43)).value(), 143);
        CHECK(tree.find(44) == tree.end());

        CHECK(tree.find(6) != tree.end());
        CHECK_EQUAL(get<1>(*tree.find(6)).value(), 16);
        CHECK(tree.find(4) == tree.end());

        CHECK_EQUAL(tree.size(), 17);
    }

    void basic_tests_move_only(AVLTreeMoveOnlyAllocator &allocator)
    {
        avl_tree_move_only tree(allocator);

        // constructing an AVL tree

        CHECK(tree.empty());
        CHECK_EQUAL(tree.size(), 0);

        CHECK(get<1>(tree.insert(AVLTreeMoveOnly::value_type(5, MoveOnlyTestElement(15)))));

        CHECK(!tree.empty());
        CHECK_EQUAL(tree.size(), 1);

        CHECK_EQUAL(get<0>(*get<0>(tree.insert(10, move_only_test_element(110)))), 10);
        CHECK(get<1>(tree.insert(3, move_only_test_element(13))));
        CHECK_EQUAL(get<1>(*get<0>(tree.insert(AVLTreeMoveOnly::value_type(24, MoveOnlyTestElement(124))))).value(), 124);

        tree.insert(51, move_only_test_element(151));
        tree.insert(AVLTreeMoveOnly::value_type(17, MoveOnlyTestElement(117)));
        tree.insert(12, move_only_test_element(112));
        tree.insert(AVLTreeMoveOnly::value_type(13, MoveOnlyTestElement(113)));
        tree.insert(43, move_only_test_element(143));
        tree.insert(AVLTreeMoveOnly::value_type(22, MoveOnlyTestElement(122)));
        tree.insert(96, move_only_test_element(196));
        tree.insert(AVLTreeMoveOnly::value_type(64, MoveOnlyTestElement(164)));
        tree.insert(2, move_only_test_element(12));
        tree.insert(AVLTreeMoveOnly::value_type(33, MoveOnlyTestElement(133)));
        tree.insert(AVLTreeMoveOnly::value_type(57, MoveOnlyTestElement(157)));
        tree.insert(AVLTreeMoveOnly::value_type(9, MoveOnlyTestElement(19)));
        tree.insert(AVLTreeMoveOnly::value_type(68, MoveOnlyTestElement(168)));
        tree.insert(AVLTreeMoveOnly::value_type(73, MoveOnlyTestElement(173)));
        tree.insert(AVLTreeMoveOnly::value_type(81, MoveOnlyTestElement(181)));

        CHECK(get<1>(tree.insert(AVLTreeMoveOnly::value_type(6, MoveOnlyTestElement(16)))));

        CHECK_EQUAL(tree.size(), 20);

        //  Inserting an element with an existing key should fail and return an iterator to the existing element

        auto bad_insert_result = tree.insert(AVLTreeMoveOnly::value_type(57, MoveOnlyTestElement(257)));

        CHECK(!get<1>(bad_insert_result));
        CHECK_EQUAL(get<0>(*get<0>(bad_insert_result)), 57);
        CHECK_EQUAL(get<1>(*get<0>(bad_insert_result)).value(), 157);

        //  Test ordering and iteration both foward and reverse

        size_t count = 0;
        uint32_t last_key = 0;

        for (AVLTreeMoveOnly::iterator itr = tree.begin(); itr != tree.end(); itr++)
        {
            CHECK(get<0>(*itr) > last_key);
            last_key = get<0>(*itr);
            count++;
        }

        CHECK_EQUAL(tree.size(), 20);
        CHECK_EQUAL(count, 20);

        count = 0;
        last_key = 100;

        for (AVLTreeMoveOnly::iterator itr = --tree.end(); itr != tree.begin(); itr--)
        {
            CHECK(get<0>(*itr) < last_key);
            last_key = get<0>(*itr);
            count++;
        }
    }

    void basic_tests_unique_pointer(minstd::allocator<AVLTreeUniquePointer::node_type> &allocator, minstd::single_block_memory_heap &test_element_heap)
    {
        avl_tree_unique_pointer tree(allocator);

        // constructing an AVL tree

        CHECK(tree.empty());
        CHECK_EQUAL(tree.size(), 0);

        CHECK(get<1>(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(5, 15))));

        CHECK(!tree.empty());
        CHECK_EQUAL(tree.size(), 1);

        CHECK_EQUAL(get<0>(*get<0>(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(10, 110)))), 10);
        CHECK(get<1>(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(3, 13))));
        CHECK_EQUAL(get<1>(*get<0>(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(24, 124))))->value(), 124);

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
        CHECK(get<1>(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(6, 16))));

        CHECK_EQUAL(tree.size(), 20);

        //  Inserting an element with an existing key should fail and return an iterator to the existing element

        auto bad_insert_result = tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(57, 257));

        CHECK(!get<1>(bad_insert_result));
        CHECK_EQUAL(get<0>(*get<0>(bad_insert_result)), 57);
        CHECK_EQUAL(get<1>(*get<0>(bad_insert_result))->value(), 157);

        //  Test ordering and iteration both foward and reverse

        size_t count = 0;
        uint32_t last_key = 0;

        for (auto itr = tree.begin(); itr != tree.end(); itr++)
        {
            CHECK(get<0>(*itr) > last_key);
            last_key = get<0>(*itr);
            count++;
        }

        CHECK_EQUAL(tree.size(), 20);
        CHECK_EQUAL(count, 20);

        count = 0;
        last_key = 100;

        for (auto itr = --tree.end(); itr != tree.begin(); itr--)
        {
            CHECK(get<0>(*itr) < last_key);
            last_key = get<0>(*itr);
            count++;
        }

        //  Test delete twice

        CHECK(get<0>(*tree.begin()) < last_key);
        count++;

        CHECK_EQUAL(tree.size(), 20);
        CHECK_EQUAL(count, 20);

        uint32_t key_to_delete = 10;

        CHECK_EQUAL(tree.erase(key_to_delete), 1);

        count = 0;
        last_key = 0;

        for (auto itr = tree.begin(); itr != tree.end(); itr++)
        {
            CHECK(get<0>(*itr) != key_to_delete);
            CHECK(get<0>(*itr) > last_key);
            last_key = get<0>(*itr);
            count++;
        }

        CHECK_EQUAL(count, 19);

        count = 0;
        last_key = 100;

        for (auto itr = --tree.end(); itr != tree.begin(); itr--)
        {
            CHECK(get<0>(*itr) != key_to_delete);
            CHECK(get<0>(*itr) < last_key);
            last_key = get<0>(*itr);
            count++;
        }

        CHECK(get<0>(*tree.begin()) < last_key);
        count++;

        CHECK_EQUAL(tree.size(), 19);
        CHECK_EQUAL(count, 19);

        key_to_delete = 57;

        auto itr_to_erase = tree.find(key_to_delete);

        CHECK_EQUAL(get<0>(*tree.erase(itr_to_erase)), 64);

        count = 0;
        last_key = 0;

        for (auto itr = tree.begin(); itr != tree.end(); itr++)
        {
            CHECK(get<0>(*itr) != key_to_delete);
            CHECK(get<0>(*itr) > last_key);
            last_key = get<0>(*itr);
            count++;
        }

        CHECK_EQUAL(tree.size(), 18);
        CHECK_EQUAL(count, 18);

        count = 0;
        last_key = 100;

        for (auto itr = --tree.end(); itr != tree.begin(); itr--)
        {
            CHECK(get<0>(*itr) != key_to_delete);
            CHECK(get<0>(*itr) < last_key);
            last_key = get<0>(*itr);
            count++;
        }

        CHECK(get<0>(*tree.begin()) < last_key);
        count++;

        CHECK_EQUAL(count, 18);

        key_to_delete = 12;

        itr_to_erase = tree.find(key_to_delete);

        CHECK_EQUAL(get<0>(*tree.erase(itr_to_erase)), 13);

        count = 0;
        last_key = 0;

        for (auto itr = tree.begin(); itr != tree.end(); itr++)
        {
            CHECK(get<0>(*itr) != key_to_delete);
            CHECK(get<0>(*itr) > last_key);
            last_key = get<0>(*itr);
            count++;
        }

        CHECK_EQUAL(tree.size(), 17);
        CHECK_EQUAL(count, 17);

        count = 0;
        last_key = 100;

        for (auto itr = --tree.end(); itr != tree.begin(); itr--)
        {
            CHECK(get<0>(*itr) != key_to_delete);
            CHECK(get<0>(*itr) < last_key);
            last_key = get<0>(*itr);
            count++;
        }

        CHECK(get<0>(*tree.begin()) < last_key);
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
        CHECK_EQUAL(get<1>(*tree.find(43))->value(), 143);
        CHECK(tree.find(44) == tree.end());

        CHECK(tree.find(6) != tree.end());
        CHECK_EQUAL(get<1>(*tree.find(6))->value(), 16);
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

            CHECK(get<1>(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(1, 11))));
            CHECK(get<1>(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(2, 12))));
            CHECK(get<1>(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(3, 13))));
            CHECK(get<1>(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(4, 14))));
            CHECK(get<1>(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(5, 15))));
            CHECK(get<1>(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(6, 16))));

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

            CHECK(get<1>(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(1, 11))));
            CHECK(get<1>(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(2, 12))));
            CHECK(get<1>(tree.insert(CREATE_TEST_ELEMENT_UNIQUE_PTR(3, 13))));

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

            CHECK(get<1>(tree.insert(minstd::reference_wrapper(key_5), 5)));

            CHECK(!tree.empty());
            CHECK_EQUAL(tree.size(), 1);

            auto two_string = minstd::fixed_string<128>("two");

            CHECK(get<0>(*get<0>(tree.insert(minstd::reference_wrapper(key_2), 2))) == static_cast<const minstd::string &>(two_string));
            CHECK(get<1>(tree.insert(minstd::reference_wrapper(key_7), 7)));
            CHECK(get<1>(tree.insert(minstd::reference_wrapper(key_1), 1)));
            auto itr = tree.begin();

            //  Ordering is by the string key

            CHECK_EQUAL(get<1>(*itr), 5);
            itr++;
            CHECK_EQUAL(get<1>(*itr), 1);
            itr++;
            CHECK_EQUAL(get<1>(*itr), 7);
            itr++;
            CHECK_EQUAL(get<1>(*itr), 2);
            itr++;
            CHECK(itr == tree.end());
        }

        CHECK_EQUAL(0, test_heap.bytes_in_use());
        CHECK_EQUAL(0, string_test_heap.bytes_in_use());
    }
}
