// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "services/stats_db/registered_statistic.h"

namespace system_stats
{
    namespace
    {
        //  Retired statistics waiting for their database's readers to drain.

        constinit minstd::atomic<internal::RetirableStatistic *> retired_statistics{nullptr};
    }

    namespace internal
    {
        SystemStatisticsDB::StatsDBResultCodes RetireStatistic(RetirableStatistic *statistic)
        {
            if (statistic == nullptr)
            {
                return SystemStatisticsDB::StatsDBResultCodes::SUCCESS;
            }

            const SystemStatisticsDB::StatsDBResultCodes result = statistic->Remove();

            //  Never free a statistic still in the database.  NOT_FOUND means it is already gone.

            if ((result != SystemStatisticsDB::StatsDBResultCodes::SUCCESS) && (result != SystemStatisticsDB::StatsDBResultCodes::NOT_FOUND))
            {
                return SystemStatisticsDB::StatsDBResultCodes::FAILURE;
            }

            //  The removal above happens before the push, which the reclaimer's exchange acquires.

            RetirableStatistic *head = retired_statistics.load(minstd::memory_order_relaxed);

            do
            {
                statistic->next_retired_ = head;
            } while (!retired_statistics.compare_exchange_weak(head, statistic, minstd::memory_order_release, minstd::memory_order_relaxed));

            ReclaimRetiredStatistics();

            return SystemStatisticsDB::StatsDBResultCodes::SUCCESS;
        }
    } // namespace internal

    uint32_t ReclaimRetiredStatistics()
    {
        internal::RetirableStatistic *retired = retired_statistics.exchange(nullptr, minstd::memory_order_acquire);

        internal::RetirableStatistic *kept_head = nullptr;
        internal::RetirableStatistic *kept_tail = nullptr;
        uint32_t freed_count = 0;

        while (retired != nullptr)
        {
            internal::RetirableStatistic *next = retired->next_retired_;

            //  Already removed, so once no reader is running none can still hold it.

            if (retired->Database().ReadersQuiescent())
            {
                retired->Destroy();
                freed_count++;
            }
            else
            {
                retired->next_retired_ = kept_head;
                kept_head = retired;

                if (kept_tail == nullptr)
                {
                    kept_tail = retired;
                }
            }

            retired = next;
        }

        //  Put back what still has readers, ahead of anything retired meanwhile.

        if (kept_head != nullptr)
        {
            internal::RetirableStatistic *head = retired_statistics.load(minstd::memory_order_relaxed);

            do
            {
                kept_tail->next_retired_ = head;
            } while (!retired_statistics.compare_exchange_weak(head, kept_head, minstd::memory_order_release, minstd::memory_order_relaxed));
        }

        return freed_count;
    }
} // namespace system_stats
