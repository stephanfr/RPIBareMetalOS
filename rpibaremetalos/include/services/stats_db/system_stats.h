// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <string.h>

#include <atomic>
#include <lockfree/latest_value_slot_array>

#if defined(__aarch64__)
#include "devices/physical_timer.h"
#endif


namespace system_stats {

    class UserStatistic;

    class TimestampTickSource
    {
    public:

        typedef int64_t (*TickFunction)();

        //  Overridable: pass nullptr to restore the default.  Safe to call while other cores are stamping.
        static void    SetTickFunction( TickFunction fn )  { tick_function_.store((fn != nullptr) ? fn : DefaultTicks, minstd::memory_order_relaxed); }
        static int64_t CurrentTicks()                      { return tick_function_.load(minstd::memory_order_relaxed)(); }

    private:
        static int64_t DefaultTicks()
        {
#if defined(__aarch64__)
            return static_cast<int64_t>(PhysicalTimer::CurrentTicks());
#else
            return 0;           //  host builds (the unit tests): a test that needs time sets SetTickFunction()
#endif
        }

        static inline constinit minstd::atomic<TickFunction> tick_function_{DefaultTicks};
    };

    enum class StatKind : uint32_t
    {
        GAUGE = 1,      //  int64, Set()
        COUNTER,        //  int64, starts at its initial value, Increment() only
        TIMESTAMP,      //  int64 raw timer ticks of when something last happened; 0 = never; Stamp() only    
        TEXT,           //  string, always NULL-terminated
        BLOB,           //  bytes
        PROBE           //  int64 read through a function given at construction
    };

    inline const char *ToString(StatKind kind)
    {
        switch (kind)
        {
        case StatKind::GAUGE:
            return "gauge";
        case StatKind::COUNTER:
            return "counter";
        case StatKind::TIMESTAMP:
            return "timestamp";
        case StatKind::TEXT:
            return "text";
        case StatKind::BLOB:
            return "blob";
        case StatKind::PROBE:
            return "probe";
        default:
            return "unknown";
        }
    }

    //  A statistic is identified by its process id and its id, both fixed by whoever defines it.  Kernel statistics
    //      are process KERNEL_PROCESS_ID; a user process's statistics carry its process id, which starts at 1 and is
    //      never reused.

    class SystemStatistic
    {
    public:
        static constexpr uint32_t GROUP_MAX       = 24;
        static constexpr uint32_t NAME_MAX        = 32;
        static constexpr uint32_t DESCRIPTION_MAX = 80;

        static constexpr uint32_t KERNEL_PROCESS_ID = 0;

        virtual ~SystemStatistic() = default;                       //  class is pure virtual

        virtual uint32_t    id()          const noexcept = 0;

        virtual uint32_t ProcessId() const noexcept
        {
            return KERNEL_PROCESS_ID;
        }

        virtual StatKind    Kind()        const noexcept = 0;
        virtual const char* Group()       const noexcept = 0;
        virtual const char* Name()        const noexcept = 0;
        virtual const char* Description() const noexcept = 0;

        //  This statistic as a UserStatistic, or nullptr if it is not one.  The build has no RTTI, and nothing may
        //      be cast to UserStatistic on the strength of its id alone.

        virtual const UserStatistic *AsUserStatistic() const noexcept
        {
            return nullptr;
        }

        //  The current value of a GAUGE, COUNTER, TIMESTAMP or PROBE statistic, for a reader that has only a
        //      SystemStatistic -- a listing of the database, say.  False, and value unchanged, for TEXT and BLOB.

        virtual bool NumericValue(int64_t &value) const noexcept
        {
            (void)value;
            return false;
        }
    };

    class SystemStatisticType : public SystemStatistic
    {
    public:

        SystemStatisticType(uint32_t process_id,
                            uint32_t id,
                            StatKind kind,
                            const char* group,
                            const char* name,
                            const char* description)
            : process_id_(process_id),
              id_(id),
              kind_(kind)
        {
            strlcpy(this->group_, (group != nullptr) ? group : "", GROUP_MAX + 1);
            strlcpy(this->name_, (name != nullptr) ? name : "", NAME_MAX + 1);
            strlcpy(this->description_, (description != nullptr) ? description : "", DESCRIPTION_MAX + 1);
        }
        
        uint32_t id() const noexcept override
        {
            return id_;
        }

        uint32_t ProcessId() const noexcept override
        {
            return process_id_;
        }

        StatKind Kind() const noexcept override
        {
            return kind_;
        }

        const char* Group() const noexcept override
        {
            return group_;
        }

        const char* Name() const noexcept override
        {
            return name_;
        }

        const char* Description() const noexcept override
        {
            return description_;
        }


    private:

        const uint32_t      process_id_;
        const uint32_t      id_;
        const StatKind      kind_;
        char                group_[GROUP_MAX + 1]{};
        char                name_[NAME_MAX + 1]{};
        char                description_[DESCRIPTION_MAX + 1]{};
    };

    //  Every statistic constructed while static recording is open -- i.e. during static initialisation --
    //      adds itself to a list, so RegisterStaticStatistics() (stats_db.h) can register them all once the heap
    //      exists, without each statistic needing a registration call of its own.
    //
    //  RegisterStaticStatistics() closes recording before walking the list.  A statistic constructed after that
    //      (on the stack, on the heap, or late) is not recorded and must be registered explicitly.  Statistics
    //      recorded this way must have static storage duration: the list holds pointers to them forever.
    //
    //  Recording makes statistic constructors non-constexpr, so statistics are dynamically initialised: no
    //      static constructor may use a statistic, as it may not have been constructed yet.

    class StaticStatisticLink : public SystemStatistic
    {
    public:
        StaticStatisticLink(const StaticStatisticLink &) = delete;
        StaticStatisticLink &operator=(const StaticStatisticLink &) = delete;

        //  Head of the recorded list, newest first, then each statistic's successor; nullptr ends the list.

        static const StaticStatisticLink *FirstStaticStatistic() noexcept
        {
            return static_head_.load(minstd::memory_order_acquire);
        }

        const StaticStatisticLink *NextStaticStatistic() const noexcept
        {
            return next_static_;
        }

        static void CloseStaticRecording() noexcept
        {
            recording_open_.store(false, minstd::memory_order_relaxed);
        }

        static bool StaticRecordingOpen() noexcept
        {
            return recording_open_.load(minstd::memory_order_relaxed);
        }

    protected:
        StaticStatisticLink() noexcept
        {
            if (!StaticRecordingOpen())
            {
                return;
            }

            //  Insert-only push: no node is ever unlinked, so there is no ABA.

            const StaticStatisticLink *head = static_head_.load(minstd::memory_order_relaxed);

            do
            {
                next_static_ = head;
            } while (!static_head_.compare_exchange_weak(head, this, minstd::memory_order_release, minstd::memory_order_relaxed));
        }

    private:
        static inline constinit minstd::atomic<const StaticStatisticLink *> static_head_{nullptr};
        static inline constinit minstd::atomic<bool>                        recording_open_{true};

        const StaticStatisticLink *next_static_ = nullptr;
    };

    //  A nullptr group, name or description is stored as the empty string, so readers never see nullptr.

    //  A statistic created at run time and registered with SystemStatisticsDB::RegisterDynamic().  Unlike the
    //      compile-time statistics it can be removed (RemoveDynamic()); see SystemStatisticsDB for when its memory
    //      may then be freed.

    class DynamicStatistic : public SystemStatistic
    {
    };

    template<uint32_t Id>
    class SystemStatisticBase : public StaticStatisticLink
    {
    public:
        SystemStatisticBase( const char*      group,
                             const char*      name,
                             const char*      description )
            : group_((group != nullptr) ? group : ""),
              name_((name != nullptr) ? name : ""),
              description_((description != nullptr) ? description : "")
        {}

        virtual StatKind Kind() const noexcept = 0;

        uint32_t id() const noexcept override
        {
            return Id;
        }

        const char* Group() const noexcept override
        {
            return group_;
        }

        const char* Name() const noexcept override
        {
            return name_;
        }

        const char* Description() const noexcept override
        {
            return description_;
        }

        bool IsNumeric() const
        {
            return (Kind() == StatKind::GAUGE) || (Kind() == StatKind::COUNTER) || (Kind() == StatKind::TIMESTAMP);
        }
        
        bool IsVariable() const
        {
            return (Kind() == StatKind::TEXT) || (Kind() == StatKind::BLOB);
        }

        bool IsProbe() const
        {
            return Kind() == StatKind::PROBE;
        }

        bool IsTimestamp() const
        {
            return Kind() == StatKind::TIMESTAMP;
        }

    protected:

        const char*                 group_;
        const char*                 name_;
        const char*                 description_;
    };


    template<uint32_t Id>
    class GaugeStatistic : public SystemStatisticBase<Id>
    {
    public:

        GaugeStatistic( const char*      group,
                        const char*      name,
                        const char*      description,
                        int64_t          initial_value )
            : SystemStatisticBase<Id>(group, name, description),
              value_(initial_value)
        {}

        StatKind Kind() const noexcept override
        {
            return StatKind::GAUGE;
        }

        bool NumericValue(int64_t &value) const noexcept override
        {
            value = Get();
            return true;
        }

        void Set( int64_t new_value )
        {
            value_.store(new_value, minstd::memory_order_relaxed);
        }

        //  Adds amount atomically; returns the previous value.  For a gauge that moves in both
        //      directions (a free-page count, say) where a read-modify-write through Set() would
        //      race between cores.

        int64_t Increment( int64_t amount = 1 )
        {
            return value_.fetch_add(amount, minstd::memory_order_relaxed);
        }

        int64_t Get() const
        {
            return value_.load(minstd::memory_order_relaxed);
        }

    private:

        minstd::atomic<int64_t> value_;
    };


    template<uint32_t Id>
    class CounterStatistic : public SystemStatisticBase<Id>
    {
    public:

        CounterStatistic( const char*      group,
                          const char*      name,
                          const char*      description,
                          int64_t          initial_value )
            : SystemStatisticBase<Id>(group, name, description),
              value_(initial_value)
        {}

        StatKind Kind() const noexcept override
        {
            return StatKind::COUNTER;
        }

        bool NumericValue(int64_t &value) const noexcept override
        {
            value = Get();
            return true;
        }

        int64_t Get() const
        {
            return value_.load(minstd::memory_order_relaxed);
        }

        int64_t Increment(int64_t amount = 1)
        {
            return value_.fetch_add(amount, minstd::memory_order_relaxed);
        }

    private:

        minstd::atomic<int64_t> value_;
    };

    template<uint32_t Id>
    class TimestampStatistic : public SystemStatisticBase<Id>
    {
    public:

        TimestampStatistic( const char*   group,
                            const char*   name,
                            const char*   description )
            : SystemStatisticBase<Id>(group, name, description),
              value_(0)
        {}

        StatKind Kind() const noexcept override
        {
            return StatKind::TIMESTAMP;
        }

        bool NumericValue(int64_t &value) const noexcept override
        {
            value = Get();
            return true;
        }

        void    Stamp()
        {
            Stamp(TimestampTickSource::CurrentTicks());
        }

        void    Stamp( int64_t ticks )
        {
            value_.store(ticks, minstd::memory_order_relaxed);
        }
        
        int64_t Get() const
        {
            return value_.load(minstd::memory_order_relaxed);
        }
        
        bool    HasOccurred() const
        {
            return Get() != 0;
        }

    private:
        minstd::atomic<int64_t>     value_;
    };

    //  Lock-free text statistic: the latest string set, up to MaxLen characters.
    //
    //  Set() never blocks.  If another Set() is in flight it returns FAILURE and drops its value -- the
    //      concurrent writer wins, which is the right outcome for a latest-value statistic.  Get() returns EMPTY
    //      (and the empty string) until the first successful Set().  See minstd::lockfree::latest_value_text.

    template<uint32_t Id, uint32_t MaxLen, uint32_t NumSlots = 4>
    class TextStatistic : public SystemStatisticBase<Id>
    {
    public:
        static constexpr uint32_t MAX_LEN   = MaxLen;
        static constexpr uint32_t NUM_SLOTS = NumSlots;

        TextStatistic( const char* group,
                       const char* name,
                       const char* description )
            : SystemStatisticBase<Id>(group, name, description)
        {}

        StatKind Kind() const noexcept override
        {
            return StatKind::TEXT;
        }

        //  nullptr sets the empty string; text longer than MaxLen is stored truncated and returns TRUNCATED.

        minstd::lockfree::latest_value_result Set( const char* text )
        {
            return value_.store(text);
        }

        //  Always null-terminates when dest_len > 0; TRUNCATED if dest was too small.

        minstd::lockfree::latest_value_result Get( char* dest, uint32_t dest_len ) const
        {
            return value_.load(dest, dest_len);
        }

        uint32_t Length() const
        {
            return static_cast<uint32_t>(value_.length());
        }

        bool HasValue() const
        {
            return value_.has_value();
        }

    private:

        minstd::lockfree::latest_value_text<char, MaxLen, NumSlots> value_;
    };

    //  Lock-free blob statistic: the latest byte buffer set, up to MaxBytes bytes.  Set() and Get() results as for
    //      TextStatistic.  See minstd::lockfree::latest_value_blob.

    template<uint32_t Id, uint32_t MaxBytes, uint32_t NumSlots = 4>
    class BlobStatistic : public SystemStatisticBase<Id>
    {
    public:

        static constexpr uint32_t MAX_BYTES = MaxBytes;
        static constexpr uint32_t NUM_SLOTS = NumSlots;

        BlobStatistic( const char* group,
                       const char* name,
                       const char* description )
            : SystemStatisticBase<Id>(group, name, description)
        {}

        StatKind Kind() const noexcept override
        {
            return StatKind::BLOB;
        }

        //  src may be nullptr only when length is 0; more than MaxBytes is stored truncated and returns TRUNCATED.

        minstd::lockfree::latest_value_result Set( const void* src, uint32_t length )
        {
            return value_.store(src, length);
        }

        //  Copies min(Size(), dest_len) bytes; TRUNCATED if dest was too small.

        minstd::lockfree::latest_value_result Get( void* dest, uint32_t dest_len ) const
        {
            return value_.load(dest, dest_len);
        }

        uint32_t Size() const
        {
            return static_cast<uint32_t>(value_.size());
        }

        bool HasValue() const
        {
            return value_.has_value();
        }

    private:

        minstd::lockfree::latest_value_blob<MaxBytes, NumSlots> value_;
    };

    //  Probe statistic: returns a computed int64 value by calling a function
    //  registered at construction.  There is no internal state — every Get()
    //  invokes the probe function directly.  The function must be safe to call
    //  from any context in which Get() is used.

    template<uint32_t Id>
    class ProbeStatistic : public SystemStatisticBase<Id>
    {
    public:
        typedef int64_t (*ProbeFunction)();

        ProbeStatistic( const char*   group,
                        const char*   name,
                        const char*   description,
                        ProbeFunction probe )
            : SystemStatisticBase<Id>(group, name, description),
              probe_(probe)
        {}

        StatKind Kind() const noexcept override
        {
            return StatKind::PROBE;
        }

        bool NumericValue(int64_t &value) const noexcept override
        {
            value = Get();
            return true;
        }

        int64_t Get() const
        {
            return probe_ ? probe_() : 0;
        }

    private:
        ProbeFunction probe_;
    };

} // namespace system_stats