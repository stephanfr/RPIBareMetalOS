// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <lockfree/intrusive_tagged_stack>
#include <lockfree/tagged_ptr>

#include <atomic>
#include <stdint.h>

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(LockfreeIntrusiveTaggedStackTests)
    {
    };
#pragma GCC diagnostic pop

    struct indexed_node
    {
        int value_;
        uint32_t next_index_;
        uint32_t aux_index_;
    };

    struct indexed_adapter
    {
        indexed_node *base_;
        uint32_t null_index_;

        indexed_node *to_node(uint32_t index) const
        {
            return (index == null_index_) ? nullptr : (base_ + index);
        }

        uint32_t to_link(const indexed_node *node) const
        {
            return (node == nullptr) ? null_index_ : static_cast<uint32_t>(node - base_);
        }
    };

    struct pointer_node
    {
        int value_;
        pointer_node *next_ptr_;
    };

    struct pointer_adapter
    {
        pointer_node *to_node(pointer_node *node) const
        {
            return node;
        }

        pointer_node *to_link(pointer_node *node) const
        {
            return node;
        }
    };

    static void no_backoff(size_t &retries)
    {
        ++retries;
    }

    TEST(LockfreeIntrusiveTaggedStackTests, IndexedNodesPushPopLifoOrder)
    {
        using tag_type = minstd::lockfree::tagged_ptr<indexed_node, uint16_t>;
        using stack_type = minstd::lockfree::intrusive_tagged_stack<indexed_node, tag_type, uint32_t, &indexed_node::next_index_>;

        indexed_node nodes[3] = {
            {1, UINT32_MAX, UINT32_MAX},
            {2, UINT32_MAX, UINT32_MAX},
            {3, UINT32_MAX, UINT32_MAX},
        };

        indexed_adapter adapter{nodes, UINT32_MAX};
        minstd::atomic<uint64_t> head{tag_type::make(nullptr)};

        stack_type::push(head, nodes[0], adapter, no_backoff);
        stack_type::push(head, nodes[1], adapter, no_backoff);
        stack_type::push(head, nodes[2], adapter, no_backoff);

        indexed_node *first = stack_type::pop(head, adapter, no_backoff);
        indexed_node *second = stack_type::pop(head, adapter, no_backoff);
        indexed_node *third = stack_type::pop(head, adapter, no_backoff);
        indexed_node *empty = stack_type::pop(head, adapter, no_backoff);

        POINTERS_EQUAL(&nodes[2], first);
        POINTERS_EQUAL(&nodes[1], second);
        POINTERS_EQUAL(&nodes[0], third);
        POINTERS_EQUAL(nullptr, empty);

        CHECK_EQUAL(UINT32_MAX, first->next_index_);
        CHECK_EQUAL(UINT32_MAX, second->next_index_);
        CHECK_EQUAL(UINT32_MAX, third->next_index_);
    }

    TEST(LockfreeIntrusiveTaggedStackTests, AlternateNextFieldTemplateParameterWorks)
    {
        using tag_type = minstd::lockfree::tagged_ptr<indexed_node, uint16_t>;
        using stack_type = minstd::lockfree::intrusive_tagged_stack<indexed_node, tag_type, uint32_t, &indexed_node::aux_index_>;

        indexed_node nodes[2] = {
            {10, UINT32_MAX, UINT32_MAX},
            {20, UINT32_MAX, UINT32_MAX},
        };

        indexed_adapter adapter{nodes, UINT32_MAX};
        minstd::atomic<uint64_t> head{tag_type::make(nullptr)};

        stack_type::push(head, nodes[0], adapter, no_backoff);
        stack_type::push(head, nodes[1], adapter, no_backoff);

        indexed_node *first = stack_type::pop(head, adapter, no_backoff);
        indexed_node *second = stack_type::pop(head, adapter, no_backoff);

        POINTERS_EQUAL(&nodes[1], first);
        POINTERS_EQUAL(&nodes[0], second);
        CHECK_EQUAL(UINT32_MAX, first->aux_index_);
        CHECK_EQUAL(UINT32_MAX, second->aux_index_);
    }

    TEST(LockfreeIntrusiveTaggedStackTests, PointerLinkedNodesWork)
    {
        using tag_type = minstd::lockfree::tagged_ptr<pointer_node, uint16_t>;
        using stack_type = minstd::lockfree::intrusive_tagged_stack<pointer_node, tag_type, pointer_node *, &pointer_node::next_ptr_>;

        pointer_node a{11, nullptr};
        pointer_node b{22, nullptr};

        pointer_adapter adapter;
        minstd::atomic<uint64_t> head{tag_type::make(nullptr)};

        stack_type::push(head, a, adapter, no_backoff);
        stack_type::push(head, b, adapter, no_backoff);

        pointer_node *first = stack_type::pop(head, adapter, no_backoff);
        pointer_node *second = stack_type::pop(head, adapter, no_backoff);

        POINTERS_EQUAL(&b, first);
        POINTERS_EQUAL(&a, second);
        POINTERS_EQUAL(nullptr, first->next_ptr_);
        POINTERS_EQUAL(nullptr, second->next_ptr_);
    }

    TEST(LockfreeIntrusiveTaggedStackTests, StealAllReturnsCurrentHeadChain)
    {
        using tag_type = minstd::lockfree::tagged_ptr<indexed_node, uint16_t>;
        using stack_type = minstd::lockfree::intrusive_tagged_stack<indexed_node, tag_type, uint32_t, &indexed_node::next_index_>;

        indexed_node nodes[3] = {
            {1, UINT32_MAX, UINT32_MAX},
            {2, UINT32_MAX, UINT32_MAX},
            {3, UINT32_MAX, UINT32_MAX},
        };

        indexed_adapter adapter{nodes, UINT32_MAX};
        minstd::atomic<uint64_t> head{tag_type::make(nullptr)};

        stack_type::push(head, nodes[0], adapter, no_backoff);
        stack_type::push(head, nodes[1], adapter, no_backoff);
        stack_type::push(head, nodes[2], adapter, no_backoff);

        indexed_node *stolen_head = stack_type::steal_all(head);

        POINTERS_EQUAL(&nodes[2], stolen_head);
        CHECK_EQUAL(1, static_cast<int>(stolen_head->next_index_));

        indexed_node *after_steal = stack_type::pop(head, adapter, no_backoff);
        POINTERS_EQUAL(nullptr, after_steal);

        CHECK_EQUAL(0, static_cast<int>(nodes[1].next_index_));
        CHECK_EQUAL(UINT32_MAX, nodes[0].next_index_);
    }
}
