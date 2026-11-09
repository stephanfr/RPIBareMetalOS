// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <memory_resource>
#include <new>
#include <utility>

#include "heaps.h"
#include "services/stats_db/stats_db.h"
#include "services/stats_db/system_stats.h"

namespace system_stats
{
    //  Frees the retired statistics no reader can still reach; returns how many.  Safe to call concurrently.

    uint32_t ReclaimRetiredStatistics();

    namespace internal
    {
        //  The type-independent part of a RegisteredStatistic allocation: enough to remove it and later free it.

        class RetirableStatistic
        {
        public:
            RetirableStatistic(const RetirableStatistic &) = delete;
            RetirableStatistic &operator=(const RetirableStatistic &) = delete;

            virtual SystemStatisticsDB::StatsDBResultCodes Remove() = 0;

            //  Nothing may still be able to reach the statistic.

            virtual void Destroy() = 0;

            SystemStatisticsDB &Database() const noexcept
            {
                return db_;
            }

        protected:

            RetirableStatistic(SystemStatisticsDB &db, minstd::pmr::memory_resource &resource) noexcept
                : db_(db),
                  resource_(resource)
            {}

            ~RetirableStatistic() = default;

            SystemStatisticsDB             &db_;
            minstd::pmr::memory_resource   &resource_;

        private:

            friend SystemStatisticsDB::StatsDBResultCodes RetireStatistic(RetirableStatistic *statistic);
            friend uint32_t system_stats::ReclaimRetiredStatistics();

            RetirableStatistic             *next_retired_ = nullptr;
        };

        template<typename T>
        class DynamicStatisticHolder final : public RetirableStatistic
        {
        public:

            template<typename... Args>
            DynamicStatisticHolder(SystemStatisticsDB &db, minstd::pmr::memory_resource &resource, Args &&...args)
                : RetirableStatistic(db, resource),
                  statistic_(minstd::forward<Args>(args)...)
            {}

            T &Statistic() noexcept
            {
                return statistic_;
            }

            const T &Statistic() const noexcept
            {
                return statistic_;
            }

            SystemStatisticsDB::StatsDBResultCodes Remove() override
            {
                return db_.RemoveDynamic(statistic_);
            }

            void Destroy() override
            {
                minstd::pmr::memory_resource &resource = resource_;

                this->~DynamicStatisticHolder();
                resource.deallocate(this, sizeof(DynamicStatisticHolder), alignof(DynamicStatisticHolder));
            }

        private:

            T   statistic_;
        };

        //  Removes statistic and frees it once no reader can reach it: now, or in a later ReclaimRetiredStatistics().
        //      One that cannot be removed is never freed, and the result is FAILURE.

        SystemStatisticsDB::StatsDBResultCodes RetireStatistic(RetirableStatistic *statistic);
    } // namespace internal

    //  Owns a dynamic statistic: registers it, and on Retire() or destruction removes it and frees it once no reader
    //      can reach it.
    //
    //          RegisteredStatistic<DynamicGaugeStatistic> queue_depth;
    //
    //          queue_depth.RegisterInKernel(id, "net", "queue_depth", "Packets waiting", 0);
    //          queue_depth.Increment();
    //
    //          if (const auto depth = queue_depth.Get(); depth.Successful()) { ... *depth ... }
    //
    //  It has the operations T has.  While no statistic is held -- not registered, retired or moved from -- each
    //      does nothing and returns NOT_REGISTERED.  Move-only, and used by one task at a time; the statistic itself
    //      may be updated from any core.

    template<DynamicStatisticType T>
    class RegisteredStatistic
    {
    private:

        using Holder = internal::DynamicStatisticHolder<T>;

    public:

        RegisteredStatistic() = default;

        ~RegisteredStatistic()
        {
            Retire();
        }

        RegisteredStatistic(const RegisteredStatistic &) = delete;
        RegisteredStatistic &operator=(const RegisteredStatistic &) = delete;

        RegisteredStatistic(RegisteredStatistic &&other) noexcept
            : holder_(other.holder_)
        {
            other.holder_ = nullptr;
        }

        //  Retires the statistic this object held, then takes other's.

        RegisteredStatistic &operator=(RegisteredStatistic &&other) noexcept
        {
            if (this != &other)
            {
                Retire();

                holder_ = other.holder_;
                other.holder_ = nullptr;
            }

            return *this;
        }

        //  Register() in the kernel's database, from the kernel's dynamic heap.  args are T's constructor's.

        template<typename... Args>
        SystemStatisticsDB::StatsDBResultCodes RegisterInKernel(Args &&...args)
        {
            return Register(GetSystemStatisticsDB(), __os_dynamic_heap_resource, minstd::forward<Args>(args)...);
        }

        //  ALREADY_REGISTERED if a statistic is already held; it is kept.  On any other failure nothing is kept.

        template<typename... Args>
        SystemStatisticsDB::StatsDBResultCodes Register(SystemStatisticsDB &db, minstd::pmr::memory_resource &resource, Args &&...args)
        {
            if (holder_ != nullptr)
            {
                return SystemStatisticsDB::StatsDBResultCodes::ALREADY_REGISTERED;
            }

            void *memory = resource.allocate(sizeof(Holder), alignof(Holder));

            if (memory == nullptr)
            {
                return SystemStatisticsDB::StatsDBResultCodes::UNABLE_TO_ALLOCATE_MEMORY;
            }

            Holder *holder = new (memory) Holder(db, resource, minstd::forward<Args>(args)...);

            const SystemStatisticsDB::StatsDBResultCodes result = db.RegisterDynamic(holder->Statistic());

            if (result != SystemStatisticsDB::StatsDBResultCodes::SUCCESS)
            {
                holder->Destroy();          //  never in the database, so no reader can have seen it
                return result;
            }

            holder_ = holder;

            return SystemStatisticsDB::StatsDBResultCodes::SUCCESS;
        }

        //  Afterwards nothing is held, whatever the result.  FAILURE if the statistic could not be removed: it stays
        //      readable and is never freed.

        SystemStatisticsDB::StatsDBResultCodes Retire()
        {
            if (holder_ == nullptr)
            {
                return SystemStatisticsDB::StatsDBResultCodes::SUCCESS;
            }

            Holder *holder = holder_;

            holder_ = nullptr;

            return internal::RetireStatistic(holder);
        }

        bool Registered() const noexcept
        {
            return holder_ != nullptr;
        }

        StatisticValueResult<int64_t> Get() const noexcept
            requires requires(const T &statistic) { { statistic.Get() } -> minstd::same_as<int64_t>; }
        {
            return Registered() ? StatisticValueResult<int64_t>::Success(holder_->Statistic().Get()) : NotRegistered<int64_t>();
        }

        StatisticValueResultCodes Set(int64_t new_value) noexcept
            requires requires(T &statistic) { statistic.Set(new_value); }
        {
            if (!Registered())
            {
                return StatisticValueResultCodes::NOT_REGISTERED;
            }

            holder_->Statistic().Set(new_value);
            return StatisticValueResultCodes::SUCCESS;
        }

        //  Returns the previous value.

        StatisticValueResult<int64_t> Increment(int64_t amount = 1) noexcept
            requires requires(T &statistic) { { statistic.Increment(amount) } -> minstd::same_as<int64_t>; }
        {
            return Registered() ? StatisticValueResult<int64_t>::Success(holder_->Statistic().Increment(amount)) : NotRegistered<int64_t>();
        }

        StatisticValueResultCodes Stamp() noexcept
            requires requires(T &statistic) { statistic.Stamp(); }
        {
            if (!Registered())
            {
                return StatisticValueResultCodes::NOT_REGISTERED;
            }

            holder_->Statistic().Stamp();
            return StatisticValueResultCodes::SUCCESS;
        }

        StatisticValueResultCodes Stamp(int64_t ticks) noexcept
            requires requires(T &statistic) { statistic.Stamp(ticks); }
        {
            if (!Registered())
            {
                return StatisticValueResultCodes::NOT_REGISTERED;
            }

            holder_->Statistic().Stamp(ticks);
            return StatisticValueResultCodes::SUCCESS;
        }

        StatisticValueResult<bool> HasOccurred() const noexcept
            requires requires(const T &statistic) { { statistic.HasOccurred() } -> minstd::same_as<bool>; }
        {
            return Registered() ? StatisticValueResult<bool>::Success(holder_->Statistic().HasOccurred()) : NotRegistered<bool>();
        }

        //  Epoch-zero while none is held.

        minstd::chrono::time_point<minstd::chrono::nanoseconds> AsTimePoint() const noexcept
            requires requires(const T &statistic) { statistic.AsTimePoint(); }
        {
            return Registered() ? holder_->Statistic().AsTimePoint()
                                : minstd::chrono::time_point<minstd::chrono::nanoseconds>();
        }

        //  nullptr while none is held.

        T *Statistic() noexcept
        {
            return Registered() ? &holder_->Statistic() : nullptr;
        }

        const T *Statistic() const noexcept
        {
            return Registered() ? &holder_->Statistic() : nullptr;
        }

    private:

        Holder     *holder_ = nullptr;

        template<typename V>
        static StatisticValueResult<V> NotRegistered() noexcept
        {
            return StatisticValueResult<V>::Failure(StatisticValueResultCodes::NOT_REGISTERED);
        }
    };
} // namespace system_stats
