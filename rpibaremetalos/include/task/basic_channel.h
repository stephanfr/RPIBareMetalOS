// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <atomic>

#include "task/basic_wait_queue.h"
#include "task/tasks.h"
#include "uapi/channels.h"

namespace task
{
    //  One kernel call on a channel: a message each way, and optionally bulk data each way through the channel's data
    //      area.  Call() sets reply_length and data_in_length.

    struct ChannelCall
    {
        const void *request = nullptr;
        size_t request_length = 0;

        void *reply = nullptr;
        size_t reply_capacity = 0;

        const void *data_out = nullptr;         //  to the driver, at most the channel's data size
        size_t data_out_length = 0;

        void *data_in = nullptr;                //  from the driver
        size_t data_in_capacity = 0;

        size_t reply_length = 0;
        size_t data_in_length = 0;
    };

    //  Kernel callers' requests to the driver process serving the channel, one at a time, and its replies.  Both travel
    //      in memory shared with the driver (uapi/channels.h): a control page for the messages, then a data area for
    //      bulk data.  The kernel's view of it is set with SetMemory().
    //
    //      channel.Call(&request, sizeof(request), &reply, sizeof(reply), reply_length, milliseconds(100));    //  kernel
    //      int length = channel.Receive(data_length);  ...  channel.Reply(reply_length, reply_data_length);    //  driver
    //
    //  The driver can't hang a caller: a call fails at its timeout, or as soon as the driver exits (Unbind()).  A
    //      request the driver took before its caller gave up still holds the channel until the driver replies to it,
    //      and that reply is dropped.  Callers wait their turn, each within its own timeout.
    //
    //  The driver may write its memory at any time, so a reply and its data are untrusted.  Receive() and Reply() are
    //      for the channel's driver alone: the system calls check that the caller serves the channel.  Policy is as for
    //      BasicWaitQueue.

    template <typename Policy>
    class BasicChannel
    {
    private:
        typedef enum class State : uint32_t
        {
            IDLE = 0,
            CLAIMED,        //  a caller is copying its request into the shared memory
            REQUESTED,
            SERVING,        //  the driver has the request
            ABANDONED,      //  the driver has the request, but its caller gave up
            REPLIED,
            FAILED,         //  the driver exited holding the request
        } State;

        static constexpr uint32_t NO_SERVER = 0;        //  process ids start at 1

        //  The channel's lock, with IRQs off: a waker never runs under it.

        class Locked
        {
        public:
            explicit Locked(typename Policy::Lock &lock)
                : lock_(lock)
            {
                lock_.Lock();
            }

            ~Locked()
            {
                lock_.Unlock();
            }

            Locked(const Locked &) = delete;
            Locked &operator=(const Locked &) = delete;

        private:
            typename Policy::IRQGuard irqs_;
            typename Policy::Lock &lock_;
        };

        using WaitQueue = BasicWaitQueue<Policy>;

    public:
        BasicChannel() = default;

        BasicChannel(const BasicChannel &) = delete;
        BasicChannel(BasicChannel &&) = delete;

        BasicChannel &operator=(const BasicChannel &) = delete;
        BasicChannel &operator=(BasicChannel &&) = delete;

        //  CHANNEL_DATA_OFFSET + data_size bytes, kept for the channel's lifetime.  Before any Bind().

        void SetMemory(uint8_t *memory, size_t data_size)
        {
            memory_ = memory;
            data_size_ = data_size;
        }

        uint8_t *Memory() const
        {
            return memory_;
        }

        size_t DataSize() const
        {
            return data_size_;
        }

        //  The process serving the channel, or 0.

        uint32_t Server() const
        {
            return server_.load(minstd::memory_order_acquire);
        }

        //  False if another process serves the channel; true, too, if 'server' already does.

        bool Bind(uint32_t server)
        {
            if ((server == NO_SERVER) || (memory_ == nullptr))
            {
                return false;
            }

            Locked locked(lock_);

            const uint32_t current = server_.load(minstd::memory_order_relaxed);

            if (current == NO_SERVER)
            {
                server_.store(server, minstd::memory_order_release);
                return true;
            }

            return current == server;
        }

        //  Ends the driver's receive with CHANNEL_EVENT, or its next one if it isn't waiting.  Events merge until a
        //      receive returns one, so the driver checks every source of them each time.  Any context, ISRs included.

        void Signal()
        {
            if (!event_.exchange(true, minstd::memory_order_acq_rel))
            {
                driver_queue_.WakeAll();
            }
        }

        //  Fails the call whose request the driver holds.  Nothing if 'server' doesn't serve the channel.

        void Unbind(uint32_t server)
        {
            {
                Locked locked(lock_);

                if ((server == NO_SERVER) || (server_.load(minstd::memory_order_relaxed) != server))
                {
                    return;
                }

                server_.store(NO_SERVER, minstd::memory_order_release);
                event_.store(false, minstd::memory_order_relaxed);

                switch (GetState())
                {
                case State::REQUESTED:
                case State::SERVING:
                    SetState(State::FAILED);
                    break;

                case State::ABANDONED:
                    SetState(State::IDLE);
                    break;

                default:
                    break;
                }
            }

            reply_queue_.WakeAll();
            claim_queue_.WakeAll();
        }

        //  CHANNEL_SUCCESS, or CHANNEL_NO_SERVER, CHANNEL_SERVER_EXITED, CHANNEL_TIMED_OUT or CHANNEL_TOO_LONG (the
        //      request or its data).  Data the driver sends back is cut to call.data_in_capacity.

        int Call(ChannelCall &call, microseconds timeout)
        {
            call.reply_length = 0;
            call.data_in_length = 0;

            if ((call.request_length > CHANNEL_MESSAGE_MAX) || (call.data_out_length > data_size_))
            {
                return CHANNEL_TOO_LONG;
            }

            const uint64_t deadline_ticks = Policy::DeadlineAfter(timeout);

            const int claimed = Claim(deadline_ticks);

            if (claimed != CHANNEL_SUCCESS)
            {
                return claimed;
            }

            memcpy(memory_ + CHANNEL_REQUEST_OFFSET, call.request, call.request_length);

            if (call.data_out_length > 0)
            {
                memcpy(memory_ + CHANNEL_DATA_OFFSET, call.data_out, call.data_out_length);
            }

            bool unserved = false;

            {
                Locked locked(lock_);

                unserved = (Server() == NO_SERVER);

                if (unserved)
                {
                    SetState(State::IDLE);
                }
                else
                {
                    request_length_ = call.request_length;
                    request_data_length_ = call.data_out_length;
                    reply_capacity_ = (call.reply_capacity < CHANNEL_MESSAGE_MAX) ? call.reply_capacity : CHANNEL_MESSAGE_MAX;
                    data_in_capacity_ = (call.data_in_capacity < data_size_) ? call.data_in_capacity : data_size_;
                    SetState(State::REQUESTED);
                }
            }

            if (unserved)
            {
                claim_queue_.WakeAll();
                return CHANNEL_NO_SERVER;
            }

            driver_queue_.WakeOne();

            return AwaitReply(call, deadline_ticks);
        }

        //  A call with messages only.

        int Call(const void *request, size_t request_length, void *reply, size_t reply_capacity, size_t &reply_length, microseconds timeout)
        {
            ChannelCall call;

            call.request = request;
            call.request_length = request_length;
            call.reply = reply;
            call.reply_capacity = reply_capacity;

            const int result = Call(call, timeout);

            reply_length = call.reply_length;

            return result;
        }

        //  For the driver.  The request's length, with the length of the data it carries in data_length, or
        //      CHANNEL_TIMED_OUT or CHANNEL_REPLY_PENDING.

        int Receive(size_t &data_length)
        {
            return ReceiveUntil(0, data_length);
        }

        int Receive(microseconds timeout, size_t &data_length)
        {
            return ReceiveUntil(Policy::DeadlineAfter(timeout), data_length);
        }

        int Receive(microseconds timeout)
        {
            size_t data_length = 0;

            return ReceiveUntil(Policy::DeadlineAfter(timeout), data_length);
        }

        //  For the driver: the reply is in the control page and its data at the start of the data area.
        //      CHANNEL_ABANDONED when its caller gave up, which frees the channel all the same.

        int Reply(size_t length, size_t data_length = 0)
        {
            if ((length > CHANNEL_MESSAGE_MAX) || (data_length > data_size_))
            {
                return CHANNEL_TOO_LONG;
            }

            bool abandoned = false;

            {
                Locked locked(lock_);

                switch (GetState())
                {
                case State::SERVING:
                    if ((length > reply_capacity_) || (data_length > data_in_capacity_))
                    {
                        return CHANNEL_TOO_LONG;
                    }

                    reply_length_ = length;
                    reply_data_length_ = data_length;
                    SetState(State::REPLIED);
                    break;

                case State::ABANDONED:
                    SetState(State::IDLE);
                    abandoned = true;
                    break;

                default:
                    return CHANNEL_NOT_SERVING;
                }
            }

            if (abandoned)
            {
                claim_queue_.WakeOne();
                return CHANNEL_ABANDONED;
            }

            reply_queue_.WakeAll();

            return CHANNEL_SUCCESS;
        }

    private:
        uint8_t *memory_ = nullptr;
        size_t data_size_ = 0;

        typename Policy::Lock lock_;

        minstd::atomic<uint32_t> state_{static_cast<uint32_t>(State::IDLE)};
        minstd::atomic<uint32_t> server_{NO_SERVER};
        minstd::atomic<bool> event_{false};

        //  Under lock_, for the call in flight.

        size_t request_length_ = 0;
        size_t request_data_length_ = 0;
        size_t reply_capacity_ = 0;
        size_t data_in_capacity_ = 0;
        size_t reply_length_ = 0;
        size_t reply_data_length_ = 0;

        WaitQueue claim_queue_;         //  callers waiting their turn
        WaitQueue reply_queue_;         //  the caller in flight
        WaitQueue driver_queue_;        //  the driver, for a request

        State GetState() const
        {
            return static_cast<State>(state_.load(minstd::memory_order_acquire));
        }

        void SetState(State state)
        {
            state_.store(static_cast<uint32_t>(state), minstd::memory_order_release);
        }

        int Claim(uint64_t deadline_ticks)
        {
            for (;;)
            {
                const auto outcome = claim_queue_.WaitOnce([this]() { return (Server() == NO_SERVER) || (GetState() == State::IDLE); },
                                                           WaitReason::IO, deadline_ticks, WaitQueue::Position::TAIL);

                {
                    Locked locked(lock_);

                    if (Server() == NO_SERVER)
                    {
                        return CHANNEL_NO_SERVER;
                    }

                    if (GetState() == State::IDLE)
                    {
                        SetState(State::CLAIMED);
                        return CHANNEL_SUCCESS;
                    }
                }

                if (outcome == WaitQueue::Outcome::TIMED_OUT)
                {
                    return CHANNEL_TIMED_OUT;
                }
            }
        }

        int AwaitReply(ChannelCall &call, uint64_t deadline_ticks)
        {
            for (;;)
            {
                const auto outcome = reply_queue_.WaitOnce([this]()
                                                           {
                                                               const State state = GetState();
                                                               return (state == State::REPLIED) || (state == State::FAILED);
                                                           },
                                                           WaitReason::IO, deadline_ticks, WaitQueue::Position::TAIL);

                bool withdrawn = false;
                bool replied = false;

                {
                    Locked locked(lock_);

                    switch (GetState())
                    {
                    case State::REPLIED:
                        call.reply_length = reply_length_;
                        call.data_in_length = reply_data_length_;
                        replied = true;
                        break;

                    case State::FAILED:
                        break;

                    case State::REQUESTED:
                        if (outcome != WaitQueue::Outcome::TIMED_OUT)
                        {
                            continue;
                        }

                        SetState(State::IDLE);
                        withdrawn = true;
                        break;

                    default:
                        if (outcome != WaitQueue::Outcome::TIMED_OUT)
                        {
                            continue;
                        }

                        SetState(State::ABANDONED);
                        return CHANNEL_TIMED_OUT;
                    }
                }

                if (withdrawn)
                {
                    claim_queue_.WakeOne();
                    return CHANNEL_TIMED_OUT;
                }

                //  REPLIED and FAILED are left only here, so the reply can't change under the copy but at the driver's
                //      own hand.

                if (replied)
                {
                    memcpy(call.reply, memory_ + CHANNEL_REPLY_OFFSET, call.reply_length);

                    if (call.data_in_length > 0)
                    {
                        memcpy(call.data_in, memory_ + CHANNEL_DATA_OFFSET, call.data_in_length);
                    }
                }

                {
                    Locked locked(lock_);

                    SetState(State::IDLE);
                }

                claim_queue_.WakeOne();

                return replied ? CHANNEL_SUCCESS : CHANNEL_SERVER_EXITED;
            }
        }

        int ReceiveUntil(uint64_t deadline_ticks, size_t &data_length)
        {
            data_length = 0;

            {
                const State state = GetState();

                if ((state == State::SERVING) || (state == State::ABANDONED))
                {
                    return CHANNEL_REPLY_PENDING;
                }
            }

            for (;;)
            {
                const auto outcome = driver_queue_.WaitOnce([this]() { return (GetState() == State::REQUESTED) || event_.load(minstd::memory_order_acquire); },
                                                            WaitReason::CHANNEL, deadline_ticks, WaitQueue::Position::TAIL);

                {
                    Locked locked(lock_);

                    if (GetState() == State::REQUESTED)
                    {
                        SetState(State::SERVING);
                        data_length = request_data_length_;
                        return static_cast<int>(request_length_);
                    }
                }

                if (event_.exchange(false, minstd::memory_order_acq_rel))
                {
                    return CHANNEL_EVENT;
                }

                if (outcome == WaitQueue::Outcome::TIMED_OUT)
                {
                    return CHANNEL_TIMED_OUT;
                }

                if (outcome == WaitQueue::Outcome::INTERRUPTED)
                {
                    return CHANNEL_INTERRUPTED;
                }
            }
        }
    };
} // namespace task
