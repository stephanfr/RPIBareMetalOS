// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "task/timer_list.h"

namespace
{
    struct TimerNode
    {
        explicit TimerNode(uint64_t deadline, uint32_t id = 0)
            : wake_deadline_ticks_(deadline), id_(id)
        {
        }

        uint64_t wake_deadline_ticks_;
        uint32_t id_;
        TimerNode *timer_next_ = nullptr;
        TimerNode *timer_prev_ = nullptr;
    };

    using TimerList = task::TimerList<TimerNode>;

    //  Expires everything and records the ids in the order they came off.

    uint32_t DrainIds(TimerList &list, uint32_t *ids, uint32_t capacity)
    {
        uint32_t count = 0;

        list.ExpireUpTo(UINT64_MAX, [&](TimerNode &node)
        {
            if (count < capacity)
            {
                ids[count] = node.id_;
            }

            count++;
        });

        return count;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(TimerListOrderTests)
    {};
#pragma GCC diagnostic pop

    TEST(TimerListOrderTests, EmptyListHasNoDeadline)
    {
        TimerList list;

        CHECK_TRUE(list.Empty());
        CHECK_EQUAL(0ULL, list.EarliestDeadline());
    }

    TEST(TimerListOrderTests, InsertKeepsDeadlinesSorted)
    {
        TimerList list;
        TimerNode late(300, 3), early(100, 1), middle(200, 2), earliest(50, 0);

        list.Insert(late);
        list.Insert(early);
        list.Insert(middle);
        list.Insert(earliest);

        CHECK_EQUAL(50ULL, list.EarliestDeadline());

        uint32_t ids[4] = {};
        CHECK_EQUAL(4U, DrainIds(list, ids, 4));
        CHECK_EQUAL(0U, ids[0]);
        CHECK_EQUAL(1U, ids[1]);
        CHECK_EQUAL(2U, ids[2]);
        CHECK_EQUAL(3U, ids[3]);
        CHECK_TRUE(list.Empty());
    }

    TEST(TimerListOrderTests, EqualDeadlinesExpireInInsertionOrder)
    {
        TimerList list;
        TimerNode first(100, 1), second(100, 2), third(100, 3);

        list.Insert(first);
        list.Insert(second);
        list.Insert(third);

        uint32_t ids[3] = {};
        CHECK_EQUAL(3U, DrainIds(list, ids, 3));
        CHECK_EQUAL(1U, ids[0]);
        CHECK_EQUAL(2U, ids[1]);
        CHECK_EQUAL(3U, ids[2]);
    }

    TEST(TimerListOrderTests, RemoveUnlinksFromHeadMiddleAndTail)
    {
        TimerList list;
        TimerNode a(10, 1), b(20, 2), c(30, 3), d(40, 4), e(50, 5);

        list.Insert(a);
        list.Insert(b);
        list.Insert(c);
        list.Insert(d);
        list.Insert(e);

        list.Remove(a);         //  head
        list.Remove(c);         //  middle
        list.Remove(e);         //  tail

        CHECK_FALSE(list.Contains(a));
        CHECK_FALSE(list.Contains(c));
        CHECK_FALSE(list.Contains(e));
        CHECK_TRUE(list.Contains(b));
        CHECK_TRUE(list.Contains(d));
        CHECK_EQUAL(20ULL, list.EarliestDeadline());

        uint32_t ids[2] = {};
        CHECK_EQUAL(2U, DrainIds(list, ids, 2));
        CHECK_EQUAL(2U, ids[0]);
        CHECK_EQUAL(4U, ids[1]);
    }

    TEST(TimerListOrderTests, RemovingAnUnlinkedNodeChangesNothing)
    {
        TimerList list;
        TimerNode linked(10, 1), never_linked(5, 2);

        list.Insert(linked);

        list.Remove(never_linked);
        list.Remove(linked);
        list.Remove(linked);

        CHECK_TRUE(list.Empty());
        CHECK_FALSE(list.Contains(linked));
        POINTERS_EQUAL(nullptr, linked.timer_next_);
        POINTERS_EQUAL(nullptr, linked.timer_prev_);
    }

    TEST(TimerListOrderTests, ANodeCanBeReinsertedAfterRemoval)
    {
        TimerList list;
        TimerNode a(10, 1), b(20, 2);

        list.Insert(a);
        list.Insert(b);
        list.Remove(a);

        a.wake_deadline_ticks_ = 30;
        list.Insert(a);

        uint32_t ids[2] = {};
        CHECK_EQUAL(2U, DrainIds(list, ids, 2));
        CHECK_EQUAL(2U, ids[0]);
        CHECK_EQUAL(1U, ids[1]);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(TimerListExpiryTests)
    {};
#pragma GCC diagnostic pop

    TEST(TimerListExpiryTests, ExpiresDeadlinesAtOrBeforeNowOnly)
    {
        TimerList list;
        TimerNode past(99, 1), now(100, 2), future(101, 3);

        list.Insert(future);
        list.Insert(now);
        list.Insert(past);

        uint32_t expired = 0;
        uint64_t last_deadline = 0;

        list.ExpireUpTo(100, [&](TimerNode &node)
        {
            CHECK_FALSE(list.Contains(node));
            CHECK_TRUE(node.wake_deadline_ticks_ >= last_deadline);
            last_deadline = node.wake_deadline_ticks_;
            expired++;
        });

        CHECK_EQUAL(2U, expired);
        CHECK_TRUE(list.Contains(future));
        CHECK_EQUAL(101ULL, list.EarliestDeadline());
    }

    TEST(TimerListExpiryTests, NothingExpiresBeforeTheEarliestDeadline)
    {
        TimerList list;
        TimerNode node(100, 1);

        list.Insert(node);

        uint32_t expired = 0;
        list.ExpireUpTo(99, [&](TimerNode &) { expired++; });
        list.ExpireUpTo(0, [&](TimerNode &) { expired++; });

        CHECK_EQUAL(0U, expired);
        CHECK_TRUE(list.Contains(node));

        TimerList empty;
        empty.ExpireUpTo(UINT64_MAX, [&](TimerNode &) { expired++; });
        CHECK_EQUAL(0U, expired);
    }

    TEST(TimerListExpiryTests, TheCallbackMayReinsertTheExpiredNode)
    {
        TimerList list;
        TimerNode periodic(10, 1);

        list.Insert(periodic);

        uint32_t expired = 0;

        list.ExpireUpTo(25, [&](TimerNode &node)
        {
            expired++;
            node.wake_deadline_ticks_ += 10;
            list.Insert(node);
        });

        //  Fires at 10 and 20, then is left waiting for 30.

        CHECK_EQUAL(2U, expired);
        CHECK_EQUAL(30ULL, list.EarliestDeadline());
    }
}
