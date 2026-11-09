// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <concepts>
#include <lockfree/latest_value_slot_array>

#include "result.h"
#include "utility/tick_conversion.h"

#if defined(__aarch64__)
#include "devices/physical_timer.h"
#endif


namespace system_stats {

    class UserStatistic;

    namespace internal
    {
        template<typename T>
        class DynamicStatisticHolder;
    }

    class ScalarStatisticView;
    class TextStatisticView;
    class BlobStatisticView;
    class ArrayStatisticView;

    class TimestampTickSource
    {
    public:

        typedef int64_t (*TickFunction)();

        //  nullptr restores the default.
        static void    SetTickFunction( TickFunction fn )  { tick_function_.store((fn != nullptr) ? fn : DefaultTicks, minstd::memory_order_relaxed); }
        static int64_t CurrentTicks()                      { return tick_function_.load(minstd::memory_order_relaxed)(); }

        //  Counter frequency in Hz, for turning ticks into nanoseconds.  A stored 0 means "not overridden": on target
        //      that reads the live counter, on host it stays 0 so a conversion reads as epoch-zero until a test sets it.
        static void     SetTickFrequency( uint64_t hz )
        {
            tick_frequency_.store(hz, minstd::memory_order_relaxed);
        }

        static uint64_t CurrentTickFrequency()
        {
            const uint64_t hz = tick_frequency_.load(minstd::memory_order_relaxed);
#if defined(__aarch64__)
            return (hz != 0) ? hz : GetCounterFrequency();
#else
            return hz;
#endif
        }

    private:
        static int64_t DefaultTicks()
        {
#if defined(__aarch64__)
            return static_cast<int64_t>(PhysicalTimer::CurrentTicks());
#else
            return 0;           //  host builds: a test that needs time sets SetTickFunction()
#endif
        }

        static inline constinit minstd::atomic<TickFunction> tick_function_{DefaultTicks};
        static inline constinit minstd::atomic<uint64_t>     tick_frequency_{0};
    };

    //  Raw counter ticks as an absolute time point, the same conversion PhysicalTimer::Now() uses; zero ticks or an
    //      unknown frequency read as epoch-zero.

    inline minstd::chrono::time_point<minstd::chrono::nanoseconds> TicksToTimePoint( int64_t ticks ) noexcept
    {
        const uint64_t frequency = TimestampTickSource::CurrentTickFrequency();

        if ((frequency == 0) || (ticks <= 0))
        {
            return minstd::chrono::time_point<minstd::chrono::nanoseconds>();
        }

        return minstd::chrono::time_point<minstd::chrono::nanoseconds>(
            minstd::chrono::nanoseconds(utility::TicksToNanoseconds(static_cast<uint64_t>(ticks), frequency)));
    }

    enum class StatKind : uint32_t
    {
        GAUGE = 1,
        COUNTER,
        TIMESTAMP,      //  raw timer ticks; 0 = never
        TEXT,
        BLOB,
        PROBE,          //  read through a function given at construction
        ARRAY
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
        case StatKind::ARRAY:
            return "array";
        default:
            return "unknown";
        }
    }

    //  How a value is held, and so which view reads it: a gauge and a counter are different kinds, both SCALAR.

    typedef enum class ValueRepresentation : uint32_t
    {
        UNKNOWN = 0,
        SCALAR,
        TEXT,
        BLOB,
        ARRAY
    } ValueRepresentation;

    constexpr ValueRepresentation RepresentationOf(StatKind kind)
    {
        switch (kind)
        {
        case StatKind::GAUGE:
        case StatKind::COUNTER:
        case StatKind::TIMESTAMP:
        case StatKind::PROBE:
            return ValueRepresentation::SCALAR;
        case StatKind::TEXT:
            return ValueRepresentation::TEXT;
        case StatKind::BLOB:
            return ValueRepresentation::BLOB;
        case StatKind::ARRAY:
            return ValueRepresentation::ARRAY;
        default:
            return ValueRepresentation::UNKNOWN;
        }
    }

    inline const char *ToString(ValueRepresentation representation)
    {
        switch (representation)
        {
        case ValueRepresentation::SCALAR:
            return "scalar";
        case ValueRepresentation::TEXT:
            return "text";
        case ValueRepresentation::BLOB:
            return "blob";
        case ValueRepresentation::ARRAY:
            return "array";
        default:
            return "unknown";
        }
    }

    typedef enum class StatisticValueResultCodes : uint32_t
    {
        SUCCESS = 0,

        INVALID_VALUE_TYPE,
        INDEX_OUT_OF_RANGE,
        NOT_REGISTERED
    } StatisticValueResultCodes;

    template<typename T>
    using StatisticValueResult = ValueResult<StatisticValueResultCodes, T>;

    template<typename T>
    concept StatisticView = minstd::same_as<T, ScalarStatisticView> ||
                            minstd::same_as<T, TextStatisticView> ||
                            minstd::same_as<T, BlobStatisticView> ||
                            minstd::same_as<T, ArrayStatisticView>;

    //  A scalar statistic's value, and each element of an array statistic.

    template<typename T>
    concept ScalarStatisticValue = minstd::same_as<T, int64_t>;

    //  Every type SystemStatistic::GetValue<T>() can return.

    template<typename T>
    concept StatisticValue = ScalarStatisticValue<T>;

    //  A statistic is identified by (process id, id); kernel statistics are process KERNEL_PROCESS_ID.  The value is
    //      read through the view for its representation.  The hierarchy is closed -- only the four views may derive
    //      from SystemStatistic, and each makes Representation() final -- which is what makes As<View>() safe
    //      without RTTI.
    //
    //  Holding a concrete statistic or a view, a wrong GetValue<T>() does not compile.  Holding only a
    //      SystemStatistic, a statistic of another representation is reported at run time: INVALID_VALUE_TYPE from
    //      GetValue<T>(), nullptr from As<View>().

    class SystemStatistic
    {
    public:
        static constexpr uint32_t GROUP_MAX       = 24;
        static constexpr uint32_t NAME_MAX        = 32;
        static constexpr uint32_t DESCRIPTION_MAX = 80;

        static constexpr uint32_t KERNEL_PROCESS_ID = 0;

        virtual ~SystemStatistic() = default;

        SystemStatistic(const SystemStatistic &) = delete;
        SystemStatistic &operator=(const SystemStatistic &) = delete;

        virtual uint32_t    id()          const noexcept = 0;

        virtual uint32_t ProcessId() const noexcept
        {
            return KERNEL_PROCESS_ID;
        }

        virtual StatKind    Kind()        const noexcept = 0;
        virtual const char* Group()       const noexcept = 0;
        virtual const char* Name()        const noexcept = 0;
        virtual const char* Description() const noexcept = 0;

        virtual ValueRepresentation Representation() const noexcept = 0;

        //  nullptr unless this is a UserStatistic.

        virtual const UserStatistic *AsUserStatistic() const noexcept
        {
            return nullptr;
        }

        //  nullptr if the statistic has another representation.

        template<StatisticView View>
        const View *As() const noexcept
        {
            return (Representation() == View::REPRESENTATION) ? static_cast<const View *>(this) : nullptr;
        }

        //  A scalar statistic's value; INVALID_VALUE_TYPE for any other representation.

        template<StatisticValue T>
        StatisticValueResult<T> GetValue() const noexcept;

        //  An array statistic's element; INVALID_VALUE_TYPE for any other representation.

        template<StatisticValue T>
        StatisticValueResult<T> GetValue(uint32_t index) const noexcept;

    private:

        SystemStatistic() = default;

        friend class ScalarStatisticView;
        friend class TextStatisticView;
        friend class BlobStatisticView;
        friend class ArrayStatisticView;
    };

    //  The views.  Each has only the accessors its representation really has; the GetValue() overloads that cannot
    //      apply are deleted, so they fail to compile rather than at run time.

    class ScalarStatisticView : public SystemStatistic
    {
    public:
        static constexpr ValueRepresentation REPRESENTATION = ValueRepresentation::SCALAR;

        ValueRepresentation Representation() const noexcept final
        {
            return REPRESENTATION;
        }

        virtual int64_t Get() const noexcept = 0;

        template<ScalarStatisticValue T>
        StatisticValueResult<T> GetValue() const noexcept
        {
            return StatisticValueResult<T>::Success(Get());
        }

        //  A scalar statistic has no elements.

        template<typename T>
        StatisticValueResult<T> GetValue(uint32_t index) const noexcept = delete;

    protected:
        ScalarStatisticView() = default;
    };

    class TextStatisticView : public SystemStatistic
    {
    public:
        static constexpr ValueRepresentation REPRESENTATION = ValueRepresentation::TEXT;

        ValueRepresentation Representation() const noexcept final
        {
            return REPRESENTATION;
        }

        virtual uint32_t MaxLength() const noexcept = 0;

        //  Always null-terminates when dest_len > 0; TRUNCATED if dest was too small, EMPTY before the first Set().

        virtual minstd::lockfree::latest_value_result Get(char *dest, uint32_t dest_len) const noexcept = 0;

        virtual uint32_t Length() const noexcept = 0;
        virtual bool     HasValue() const noexcept = 0;

        //  Text has no scalar value: read it with Get(dest, dest_len).

        template<typename T>
        StatisticValueResult<T> GetValue() const noexcept = delete;

        template<typename T>
        StatisticValueResult<T> GetValue(uint32_t index) const noexcept = delete;

    protected:
        TextStatisticView() = default;
    };

    class BlobStatisticView : public SystemStatistic
    {
    public:
        static constexpr ValueRepresentation REPRESENTATION = ValueRepresentation::BLOB;

        ValueRepresentation Representation() const noexcept final
        {
            return REPRESENTATION;
        }

        virtual uint32_t MaxSize() const noexcept = 0;

        //  Copies min(Size(), dest_len) bytes; TRUNCATED if dest was too small, EMPTY before the first Set().

        virtual minstd::lockfree::latest_value_result Get(void *dest, uint32_t dest_len) const noexcept = 0;

        virtual uint32_t Size() const noexcept = 0;
        virtual bool     HasValue() const noexcept = 0;

        //  A blob has no scalar value: read it with Get(dest, dest_len).

        template<typename T>
        StatisticValueResult<T> GetValue() const noexcept = delete;

        template<typename T>
        StatisticValueResult<T> GetValue(uint32_t index) const noexcept = delete;

    protected:
        BlobStatisticView() = default;
    };

    class ArrayStatisticView : public SystemStatistic
    {
    public:
        static constexpr ValueRepresentation REPRESENTATION = ValueRepresentation::ARRAY;

        ValueRepresentation Representation() const noexcept final
        {
            return REPRESENTATION;
        }

        //  Fixed for the life of the statistic.

        virtual uint32_t Count() const noexcept = 0;

        //  INDEX_OUT_OF_RANGE at or past Count().

        template<ScalarStatisticValue T>
        StatisticValueResult<T> GetValue(uint32_t index) const noexcept
        {
            return (index < Count()) ? StatisticValueResult<T>::Success(Element(index))
                                     : StatisticValueResult<T>::Failure(StatisticValueResultCodes::INDEX_OUT_OF_RANGE);
        }

        //  An array has no single value: read an element with GetValue<T>(index).

        template<typename T>
        StatisticValueResult<T> GetValue() const noexcept = delete;

    protected:
        ArrayStatisticView() = default;

        //  Called only with index < Count().

        virtual int64_t Element(uint32_t index) const noexcept = 0;
    };

    template<StatisticValue T>
    StatisticValueResult<T> SystemStatistic::GetValue() const noexcept
    {
        const ScalarStatisticView *scalar = As<ScalarStatisticView>();

        return (scalar != nullptr) ? scalar->GetValue<T>()
                                   : StatisticValueResult<T>::Failure(StatisticValueResultCodes::INVALID_VALUE_TYPE);
    }

    template<StatisticValue T>
    StatisticValueResult<T> SystemStatistic::GetValue(uint32_t index) const noexcept
    {
        const ArrayStatisticView *array = As<ArrayStatisticView>();

        return (array != nullptr) ? array->GetValue<T>(index)
                                  : StatisticValueResult<T>::Failure(StatisticValueResultCodes::INVALID_VALUE_TYPE);
    }

    //  A copy of one statistic's metadata, without its value.  Strings are truncated to fit; nullptr is stored as "".

    class SystemStatisticType
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
            strlcpy(this->group_, (group != nullptr) ? group : "", SystemStatistic::GROUP_MAX + 1);
            strlcpy(this->name_, (name != nullptr) ? name : "", SystemStatistic::NAME_MAX + 1);
            strlcpy(this->description_, (description != nullptr) ? description : "", SystemStatistic::DESCRIPTION_MAX + 1);
        }

        uint32_t id() const noexcept
        {
            return id_;
        }

        uint32_t ProcessId() const noexcept
        {
            return process_id_;
        }

        StatKind Kind() const noexcept
        {
            return kind_;
        }

        ValueRepresentation Representation() const noexcept
        {
            return RepresentationOf(kind_);
        }

        const char* Group() const noexcept
        {
            return group_;
        }

        const char* Name() const noexcept
        {
            return name_;
        }

        const char* Description() const noexcept
        {
            return description_;
        }

    private:

        const uint32_t      process_id_;
        const uint32_t      id_;
        const StatKind      kind_;
        char                group_[SystemStatistic::GROUP_MAX + 1]{};
        char                name_[SystemStatistic::NAME_MAX + 1]{};
        char                description_[SystemStatistic::DESCRIPTION_MAX + 1]{};
    };

    template<StatisticView View, StatKind StatisticKind, uint32_t Id>
    class StaticStatistic;

    //  Every StaticStatistic constructed during static initialisation records itself here, so
    //      RegisterStaticStatistics() (stats_db.h) can register them all at boot without a call per statistic.  That
    //      closes recording: a statistic constructed later is not recorded and must be registered explicitly.
    //
    //  The list holds pointers forever, so recorded statistics must have static storage duration.  Statistics are
    //      dynamically initialised, so no static constructor may use one: it may not have been constructed yet.

    class StaticStatisticLink
    {
    public:
        StaticStatisticLink(const StaticStatisticLink &) = delete;
        StaticStatisticLink &operator=(const StaticStatisticLink &) = delete;

        //  Newest first; nullptr ends the list.

        static const StaticStatisticLink *FirstStaticStatistic() noexcept
        {
            return static_head_.load(minstd::memory_order_acquire);
        }

        const StaticStatisticLink *NextStaticStatistic() const noexcept
        {
            return next_static_;
        }

        const SystemStatistic &Statistic() const noexcept
        {
            return statistic_;
        }

        static void CloseStaticRecording() noexcept
        {
            recording_open_.store(false, minstd::memory_order_relaxed);
        }

        static bool StaticRecordingOpen() noexcept
        {
            return recording_open_.load(minstd::memory_order_relaxed);
        }

    private:
        template<StatisticView View, StatKind StatisticKind, uint32_t Id>
        friend class StaticStatistic;

        static inline constinit minstd::atomic<const StaticStatisticLink *> static_head_{nullptr};
        static inline constinit minstd::atomic<bool>                        recording_open_{true};

        const SystemStatistic      &statistic_;
        const StaticStatisticLink  *next_static_ = nullptr;

        explicit StaticStatisticLink(const SystemStatistic &statistic) noexcept
            : statistic_(statistic)
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
    };

    //  Marks a statistic that can be registered with RegisterDynamic() and removed with RemoveDynamic().  It derives
    //      from one view and from DynamicStatistic.  Kernel services use the ready-made ones below through
    //      RegisteredStatistic (registered_statistic.h) rather than writing their own.

    class DynamicStatistic
    {
    protected:
        DynamicStatistic() = default;
        ~DynamicStatistic() = default;

        DynamicStatistic(const DynamicStatistic &) = delete;
        DynamicStatistic &operator=(const DynamicStatistic &) = delete;
    };

    template<typename T>
    concept DynamicStatisticType = minstd::derived_from<T, SystemStatistic> && minstd::derived_from<T, DynamicStatistic>;

    //  Base of the compile-time kernel statistics: the id and kind are template arguments.

    template<StatisticView View, StatKind StatisticKind, uint32_t Id>
    class StaticStatistic : public View
    {
    public:
        static constexpr StatKind KIND = StatisticKind;

        static_assert(RepresentationOf(StatisticKind) == View::REPRESENTATION, "the statistic's kind is not one its view represents");

        uint32_t id() const noexcept final
        {
            return Id;
        }

        uint32_t ProcessId() const noexcept final
        {
            return SystemStatistic::KERNEL_PROCESS_ID;
        }

        StatKind Kind() const noexcept final
        {
            return StatisticKind;
        }

        const char* Group() const noexcept final
        {
            return group_;
        }

        const char* Name() const noexcept final
        {
            return name_;
        }

        const char* Description() const noexcept final
        {
            return description_;
        }

    protected:

        //  The strings are not copied.  nullptr is stored as "".

        StaticStatistic( const char*      group,
                         const char*      name,
                         const char*      description )
            : group_((group != nullptr) ? group : ""),
              name_((name != nullptr) ? name : ""),
              description_((description != nullptr) ? description : ""),
              link_(*this)
        {}

    private:

        const char*                 group_;
        const char*                 name_;
        const char*                 description_;
        StaticStatisticLink         link_;
    };


    template<uint32_t Id>
    class GaugeStatistic final : public StaticStatistic<ScalarStatisticView, StatKind::GAUGE, Id>
    {
    public:

        GaugeStatistic( const char*      group,
                        const char*      name,
                        const char*      description,
                        int64_t          initial_value )
            : StaticStatistic<ScalarStatisticView, StatKind::GAUGE, Id>(group, name, description),
              value_(initial_value)
        {}

        void Set( int64_t new_value )
        {
            value_.store(new_value, minstd::memory_order_relaxed);
        }

        //  Atomic; returns the previous value.  Use it rather than Get() then Set(), which races between cores.

        int64_t Increment( int64_t amount = 1 )
        {
            return value_.fetch_add(amount, minstd::memory_order_relaxed);
        }

        int64_t Get() const noexcept override
        {
            return value_.load(minstd::memory_order_relaxed);
        }

    private:

        minstd::atomic<int64_t> value_;
    };


    template<uint32_t Id>
    class CounterStatistic final : public StaticStatistic<ScalarStatisticView, StatKind::COUNTER, Id>
    {
    public:

        CounterStatistic( const char*      group,
                          const char*      name,
                          const char*      description,
                          int64_t          initial_value )
            : StaticStatistic<ScalarStatisticView, StatKind::COUNTER, Id>(group, name, description),
              value_(initial_value)
        {}

        int64_t Get() const noexcept override
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
    class TimestampStatistic final : public StaticStatistic<ScalarStatisticView, StatKind::TIMESTAMP, Id>
    {
    public:

        TimestampStatistic( const char*   group,
                            const char*   name,
                            const char*   description )
            : StaticStatistic<ScalarStatisticView, StatKind::TIMESTAMP, Id>(group, name, description),
              value_(0)
        {}

        void    Stamp()
        {
            Stamp(TimestampTickSource::CurrentTicks());
        }

        void    Stamp( int64_t ticks )
        {
            value_.store(ticks, minstd::memory_order_relaxed);
        }

        int64_t Get() const noexcept override
        {
            return value_.load(minstd::memory_order_relaxed);
        }

        //  The stored ticks as an absolute time point; epoch-zero before the first Stamp().
        minstd::chrono::time_point<minstd::chrono::nanoseconds> AsTimePoint() const noexcept
        {
            return TicksToTimePoint(Get());
        }

        bool    HasOccurred() const
        {
            return Get() != 0;
        }

    private:
        minstd::atomic<int64_t>     value_;
    };

    //  The latest string set, up to MaxLen characters.  Set() never blocks: if another Set() is in flight it returns
    //      FAILURE and the concurrent writer wins.  Get() returns EMPTY until the first successful Set().

    template<uint32_t Id, uint32_t MaxLen, uint32_t NumSlots = 4>
    class TextStatistic final : public StaticStatistic<TextStatisticView, StatKind::TEXT, Id>
    {
    public:
        static constexpr uint32_t MAX_LEN   = MaxLen;
        static constexpr uint32_t NUM_SLOTS = NumSlots;

        TextStatistic( const char* group,
                       const char* name,
                       const char* description )
            : StaticStatistic<TextStatisticView, StatKind::TEXT, Id>(group, name, description)
        {}

        uint32_t MaxLength() const noexcept override
        {
            return MAX_LEN;
        }

        //  nullptr sets the empty string; text longer than MaxLen is stored truncated and returns TRUNCATED.

        minstd::lockfree::latest_value_result Set( const char* text )
        {
            return value_.store(text);
        }

        //  Always null-terminates when dest_len > 0; TRUNCATED if dest was too small.

        minstd::lockfree::latest_value_result Get( char* dest, uint32_t dest_len ) const noexcept override
        {
            return value_.load(dest, dest_len);
        }

        uint32_t Length() const noexcept override
        {
            return static_cast<uint32_t>(value_.length());
        }

        bool HasValue() const noexcept override
        {
            return value_.has_value();
        }

    private:

        minstd::lockfree::latest_value_text<char, MaxLen, NumSlots> value_;
    };

    //  The latest bytes set, up to MaxBytes.  Set() and Get() behave as for TextStatistic.

    template<uint32_t Id, uint32_t MaxBytes, uint32_t NumSlots = 4>
    class BlobStatistic final : public StaticStatistic<BlobStatisticView, StatKind::BLOB, Id>
    {
    public:

        static constexpr uint32_t MAX_BYTES = MaxBytes;
        static constexpr uint32_t NUM_SLOTS = NumSlots;

        BlobStatistic( const char* group,
                       const char* name,
                       const char* description )
            : StaticStatistic<BlobStatisticView, StatKind::BLOB, Id>(group, name, description)
        {}

        uint32_t MaxSize() const noexcept override
        {
            return MAX_BYTES;
        }

        //  src may be nullptr only when length is 0; more than MaxBytes is stored truncated and returns TRUNCATED.

        minstd::lockfree::latest_value_result Set( const void* src, uint32_t length )
        {
            return value_.store(src, length);
        }

        //  Copies min(Size(), dest_len) bytes; TRUNCATED if dest was too small.

        minstd::lockfree::latest_value_result Get( void* dest, uint32_t dest_len ) const noexcept override
        {
            return value_.load(dest, dest_len);
        }

        uint32_t Size() const noexcept override
        {
            return static_cast<uint32_t>(value_.size());
        }

        bool HasValue() const noexcept override
        {
            return value_.has_value();
        }

    private:

        minstd::lockfree::latest_value_blob<MaxBytes, NumSlots> value_;
    };

    //  Get() calls probe, so probe must be safe wherever Get() may run.  A nullptr probe reads as 0.

    template<uint32_t Id>
    class ProbeStatistic final : public StaticStatistic<ScalarStatisticView, StatKind::PROBE, Id>
    {
    public:
        typedef int64_t (*ProbeFunction)();

        ProbeStatistic( const char*   group,
                        const char*   name,
                        const char*   description,
                        ProbeFunction probe )
            : StaticStatistic<ScalarStatisticView, StatKind::PROBE, Id>(group, name, description),
              probe_(probe)
        {}

        int64_t Get() const noexcept override
        {
            return probe_ ? probe_() : 0;
        }

    private:
        ProbeFunction probe_;
    };

    //  Base of the ready-made dynamic kernel statistics: the id is given at run time and the strings are copied,
    //      so they may be built at run time.  Only RegisteredStatistic can create the concrete classes.

    template<StatisticView View, StatKind StatisticKind>
    class DynamicKernelStatistic : public View, public DynamicStatistic
    {
    public:
        static constexpr StatKind KIND = StatisticKind;

        static_assert(RepresentationOf(StatisticKind) == View::REPRESENTATION, "the statistic's kind is not one its view represents");

        uint32_t id() const noexcept final
        {
            return metadata_.id();
        }

        uint32_t ProcessId() const noexcept final
        {
            return SystemStatistic::KERNEL_PROCESS_ID;
        }

        StatKind Kind() const noexcept final
        {
            return StatisticKind;
        }

        const char* Group() const noexcept final
        {
            return metadata_.Group();
        }

        const char* Name() const noexcept final
        {
            return metadata_.Name();
        }

        const char* Description() const noexcept final
        {
            return metadata_.Description();
        }

    protected:

        DynamicKernelStatistic( uint32_t         id,
                                const char*      group,
                                const char*      name,
                                const char*      description )
            : metadata_(SystemStatistic::KERNEL_PROCESS_ID, id, StatisticKind, group, name, description)
        {}

    private:

        SystemStatisticType     metadata_;
    };

    //  The dynamic counterparts of GaugeStatistic, CounterStatistic and TimestampStatistic:
    //
    //          RegisteredStatistic<DynamicGaugeStatistic> queue_depth;
    //          queue_depth.RegisterInKernel(id, "net", "queue_depth", "Packets waiting", 0);

    class DynamicGaugeStatistic final : public DynamicKernelStatistic<ScalarStatisticView, StatKind::GAUGE>
    {
    public:

        void Set( int64_t new_value )
        {
            value_.store(new_value, minstd::memory_order_relaxed);
        }

        //  Adds amount atomically; returns the previous value.

        int64_t Increment( int64_t amount = 1 )
        {
            return value_.fetch_add(amount, minstd::memory_order_relaxed);
        }

        int64_t Get() const noexcept override
        {
            return value_.load(minstd::memory_order_relaxed);
        }

    private:

        template<typename T>
        friend class internal::DynamicStatisticHolder;

        DynamicGaugeStatistic( uint32_t         id,
                               const char*      group,
                               const char*      name,
                               const char*      description,
                               int64_t          initial_value = 0 )
            : DynamicKernelStatistic<ScalarStatisticView, StatKind::GAUGE>(id, group, name, description),
              value_(initial_value)
        {}

        minstd::atomic<int64_t> value_;
    };

    class DynamicCounterStatistic final : public DynamicKernelStatistic<ScalarStatisticView, StatKind::COUNTER>
    {
    public:

        //  Adds amount atomically; returns the previous value.

        int64_t Increment( int64_t amount = 1 )
        {
            return value_.fetch_add(amount, minstd::memory_order_relaxed);
        }

        int64_t Get() const noexcept override
        {
            return value_.load(minstd::memory_order_relaxed);
        }

    private:

        template<typename T>
        friend class internal::DynamicStatisticHolder;

        DynamicCounterStatistic( uint32_t         id,
                                 const char*      group,
                                 const char*      name,
                                 const char*      description,
                                 int64_t          initial_value = 0 )
            : DynamicKernelStatistic<ScalarStatisticView, StatKind::COUNTER>(id, group, name, description),
              value_(initial_value)
        {}

        minstd::atomic<int64_t> value_;
    };

    class DynamicTimestampStatistic final : public DynamicKernelStatistic<ScalarStatisticView, StatKind::TIMESTAMP>
    {
    public:

        void Stamp()
        {
            Stamp(TimestampTickSource::CurrentTicks());
        }

        void Stamp( int64_t ticks )
        {
            value_.store(ticks, minstd::memory_order_relaxed);
        }

        int64_t Get() const noexcept override
        {
            return value_.load(minstd::memory_order_relaxed);
        }

        //  The stored ticks as an absolute time point; epoch-zero before the first Stamp().
        minstd::chrono::time_point<minstd::chrono::nanoseconds> AsTimePoint() const noexcept
        {
            return TicksToTimePoint(Get());
        }

        bool HasOccurred() const
        {
            return Get() != 0;
        }

    private:

        template<typename T>
        friend class internal::DynamicStatisticHolder;

        DynamicTimestampStatistic( uint32_t      id,
                                   const char*   group,
                                   const char*   name,
                                   const char*   description )
            : DynamicKernelStatistic<ScalarStatisticView, StatKind::TIMESTAMP>(id, group, name, description),
              value_(0)
        {}

        minstd::atomic<int64_t> value_;
    };

    //  Gauges under one id, one per something -- run list depth per core, say.  Capacity is the compile-time maximum
    //      (MAX_CORES); count is how many elements really exist (the cores the board has), clamped to Capacity.
    //      Constructed during static initialisation, so count must be known by then.
    //
    //  A bad index does not trap: Set() ignores it, Increment() and GetValue() return INDEX_OUT_OF_RANGE.

    template<uint32_t Id, uint32_t Capacity>
    class GaugeArrayStatistic final : public StaticStatistic<ArrayStatisticView, StatKind::ARRAY, Id>
    {
    public:
        static constexpr uint32_t CAPACITY = Capacity;

        static_assert(CAPACITY > 0, "an array statistic needs room for at least one element");

        GaugeArrayStatistic( const char*      group,
                             const char*      name,
                             const char*      description,
                             int64_t          initial_value,
                             uint32_t         count = Capacity )
            : StaticStatistic<ArrayStatisticView, StatKind::ARRAY, Id>(group, name, description),
              count_((count < CAPACITY) ? count : CAPACITY)
        {
            for (uint32_t i = 0; i < CAPACITY; i++)
            {
                values_[i].store(initial_value, minstd::memory_order_relaxed);
            }
        }

        uint32_t Count() const noexcept override
        {
            return count_;
        }

        void Set(uint32_t index, int64_t new_value)
        {
            if (index < count_)
            {
                values_[index].store(new_value, minstd::memory_order_relaxed);
            }
        }

        //  Atomic; returns the element's previous value.

        StatisticValueResult<int64_t> Increment(uint32_t index, int64_t amount = 1)
        {
            if (index >= count_)
            {
                return StatisticValueResult<int64_t>::Failure(StatisticValueResultCodes::INDEX_OUT_OF_RANGE);
            }

            return StatisticValueResult<int64_t>::Success(values_[index].fetch_add(amount, minstd::memory_order_relaxed));
        }

    private:

        const uint32_t          count_;
        minstd::atomic<int64_t> values_[CAPACITY];

        int64_t Element(uint32_t index) const noexcept override
        {
            return values_[index].load(minstd::memory_order_relaxed);
        }
    };

} // namespace system_stats
