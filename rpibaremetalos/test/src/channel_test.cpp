// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "fake_wait_policy.h"

#include "task/basic_channel.h"

#include <string.h>

namespace
{
    using fake_wait::FakeTask;
    using fake_wait::FakeWaitPolicy;

    using Channel = task::BasicChannel<FakeWaitPolicy>;

    constexpr uint32_t DRIVER = 7;
    constexpr uint32_t OTHER_DRIVER = 8;
    constexpr minstd::chrono::microseconds TIMEOUT(1000);
    constexpr size_t DATA_SIZE = 0x4000;

    //  A is the kernel caller and B the driver; C is a second caller.

    FakeTask a('A'), b('B'), c('C');

    alignas(16) uint8_t page[CHANNEL_DATA_OFFSET + DATA_SIZE];

    Channel *channel = nullptr;

    //  What the hooks saw, acting as the driver or as C.

    int received = 0;
    int receive_while_serving = 0;
    int replied = 0;
    int c_called = 0;
    size_t data_received = 0;

    //  The driver's reply to a request: each byte plus one.

    size_t Answer(size_t length)
    {
        for (size_t i = 0; i < length; i++)
        {
            page[CHANNEL_REPLY_OFFSET + i] = static_cast<uint8_t>(page[CHANNEL_REQUEST_OFFSET + i] + 1);
        }

        return length;
    }

    void Reset(Channel &under_test)
    {
        FakeWaitPolicy::Reset();

        FakeTask *const tasks[] = {&a, &b, &c};

        for (FakeTask *task : tasks)
        {
            task->status_ = task::WaitStatus::NONE;
            task->blocks_ = 0;
            task->on_block_ = nullptr;
            task->on_prepare_ = nullptr;
        }

        memset(page, 0, sizeof(page));

        received = 0;
        receive_while_serving = 0;
        replied = 0;
        c_called = 0;
        data_received = 0;

        channel = &under_test;
        channel->SetMemory(page, DATA_SIZE);

        FakeWaitPolicy::current_ = &a;
    }

    //  Hooks run while A is blocked.

    void DriverEchoes(FakeTask &)
    {
        FakeWaitPolicy::current_ = &b;

        received = channel->Receive(TIMEOUT);
        receive_while_serving = channel->Receive(TIMEOUT);
        replied = channel->Reply(Answer(static_cast<size_t>(received)));
    }

    void DriverTakesTheRequest(FakeTask &)
    {
        FakeWaitPolicy::current_ = &b;

        received = channel->Receive(TIMEOUT);
    }

    void DriverRepliesTooLongThenFits(FakeTask &)
    {
        FakeWaitPolicy::current_ = &b;

        received = channel->Receive(TIMEOUT);

        CHECK_EQUAL(CHANNEL_TOO_LONG, channel->Reply(CHANNEL_MESSAGE_MAX + 1));
        CHECK_EQUAL(CHANNEL_TOO_LONG, channel->Reply(8));

        replied = channel->Reply(Answer(4));
    }

    //  The driver's answer to data: each byte of it complemented, the same length, and a reply of 1 byte.

    void DriverComplementsTheData(FakeTask &)
    {
        FakeWaitPolicy::current_ = &b;

        received = channel->Receive(TIMEOUT, data_received);

        for (size_t i = 0; i < data_received; i++)
        {
            page[CHANNEL_DATA_OFFSET + i] = static_cast<uint8_t>(~page[CHANNEL_DATA_OFFSET + i]);
        }

        page[CHANNEL_REPLY_OFFSET] = 0x5A;
        replied = channel->Reply(1, data_received);
    }

    void DriverSendsTooMuchDataThenFits(FakeTask &)
    {
        FakeWaitPolicy::current_ = &b;

        received = channel->Receive(TIMEOUT, data_received);

        CHECK_EQUAL(CHANNEL_TOO_LONG, channel->Reply(0, DATA_SIZE + 1));
        CHECK_EQUAL(CHANNEL_TOO_LONG, channel->Reply(0, 17));

        memset(page + CHANNEL_DATA_OFFSET, 0x33, 16);
        replied = channel->Reply(0, 16);
    }

    void DriverExitsHoldingTheRequest(FakeTask &)
    {
        FakeWaitPolicy::current_ = &b;

        received = channel->Receive(TIMEOUT);
        channel->Unbind(DRIVER);
    }

    void DriverExitsBeforeReceiving(FakeTask &)
    {
        channel->Unbind(DRIVER);
    }

    void DriverRepliesAsTheTimeoutPasses(FakeTask &)
    {
        DriverEchoes(a);
        FakeWaitPolicy::now_ticks_ += 10 * TIMEOUT.count();
    }

    //  While A waits its turn: the driver answers the request A's earlier call abandoned, and A is next.

    void DriverAnswersTheAbandonedRequest(FakeTask &)
    {
        FakeWaitPolicy::current_ = &b;

        CHECK_EQUAL(CHANNEL_ABANDONED, channel->Reply(0));

        a.on_block_ = DriverEchoes;
    }

    //  While A's request is in service, C calls and gives up waiting its turn; then the driver answers A.

    void SecondCallerTimesOutThenDriverEchoes(FakeTask &)
    {
        FakeWaitPolicy::current_ = &b;

        received = channel->Receive(TIMEOUT);

        FakeWaitPolicy::current_ = &c;

        uint8_t request[4] = {9, 9, 9, 9};
        uint8_t reply[4] = {};
        size_t reply_length = 0;

        c_called = channel->Call(request, sizeof(request), reply, sizeof(reply), reply_length, TIMEOUT);

        FakeWaitPolicy::current_ = &b;

        replied = channel->Reply(Answer(static_cast<size_t>(received)));
    }

    //  Runs while the driver, B, waits for a request: A calls, and gives up before the driver runs.

    void CallerGivesUpBeforeTheDriverRuns(FakeTask &)
    {
        FakeWaitPolicy::current_ = &a;

        uint8_t request[2] = {1, 2};
        uint8_t reply[2] = {};
        size_t reply_length = 0;

        c_called = channel->Call(request, sizeof(request), reply, sizeof(reply), reply_length, TIMEOUT);
    }

    //  A call of 'length' bytes, 0, 1, 2 ..., checked against DriverEchoes's answer.

    int EchoCall(size_t length)
    {
        uint8_t request[CHANNEL_MESSAGE_MAX];
        uint8_t reply[CHANNEL_MESSAGE_MAX];
        size_t reply_length = 0;

        for (size_t i = 0; i < length; i++)
        {
            request[i] = static_cast<uint8_t>(i);
            reply[i] = 0;
        }

        FakeWaitPolicy::current_ = &a;
        a.on_block_ = DriverEchoes;

        const int result = channel->Call(request, length, reply, sizeof(reply), reply_length, TIMEOUT);

        if (result == CHANNEL_SUCCESS)
        {
            CHECK_EQUAL(length, reply_length);

            for (size_t i = 0; i < length; i++)
            {
                CHECK_EQUAL(static_cast<uint8_t>(i + 1), reply[i]);
            }
        }

        return result;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(ChannelTests)
    {};
#pragma GCC diagnostic pop

    TEST(ChannelTests, OneDriverServesAChannel)
    {
        Channel unpaged;

        CHECK_FALSE(unpaged.Bind(DRIVER));

        Channel under_test;
        Reset(under_test);

        CHECK_EQUAL(0U, channel->Server());
        CHECK_FALSE(channel->Bind(0));
        CHECK_TRUE(channel->Bind(DRIVER));
        CHECK_TRUE(channel->Bind(DRIVER));
        CHECK_FALSE(channel->Bind(OTHER_DRIVER));

        channel->Unbind(OTHER_DRIVER);
        CHECK_EQUAL(DRIVER, channel->Server());

        channel->Unbind(DRIVER);
        CHECK_EQUAL(0U, channel->Server());
        CHECK_TRUE(channel->Bind(OTHER_DRIVER));
    }

    TEST(ChannelTests, CallsFailAtOnceWithNoDriver)
    {
        Channel under_test;
        Reset(under_test);

        uint8_t request[1] = {1};
        uint8_t reply[1] = {};
        size_t reply_length = 99;

        CHECK_EQUAL(CHANNEL_NO_SERVER, channel->Call(request, sizeof(request), reply, sizeof(reply), reply_length, TIMEOUT));
        CHECK_EQUAL(0U, reply_length);
        CHECK_EQUAL(0U, a.blocks_);
    }

    TEST(ChannelTests, RequestReachesTheDriverAndTheReplyComesBack)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));

        CHECK_EQUAL(CHANNEL_SUCCESS, EchoCall(3));
        CHECK_EQUAL(3, received);
        CHECK_EQUAL(CHANNEL_REPLY_PENDING, receive_while_serving);
        CHECK_EQUAL(CHANNEL_SUCCESS, replied);

        FakeWaitPolicy::current_ = &b;
        CHECK_EQUAL(CHANNEL_NOT_SERVING, channel->Reply(0));
    }

    TEST(ChannelTests, MessagesOfEveryLengthUpToTheMaximumPass)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));

        const size_t lengths[] = {0, 1, 255, 256, CHANNEL_MESSAGE_MAX - 1, CHANNEL_MESSAGE_MAX};

        for (size_t length : lengths)
        {
            CHECK_EQUAL(CHANNEL_SUCCESS, EchoCall(length));
            CHECK_EQUAL(static_cast<int>(length), received);
        }
    }

    TEST(ChannelTests, OverlongRequestsNeverReachTheDriver)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));

        static uint8_t request[CHANNEL_MESSAGE_MAX + 1];
        uint8_t reply[1] = {};
        size_t reply_length = 0;

        CHECK_EQUAL(CHANNEL_TOO_LONG, channel->Call(request, sizeof(request), reply, sizeof(reply), reply_length, TIMEOUT));

        FakeWaitPolicy::current_ = &b;
        CHECK_EQUAL(CHANNEL_TIMED_OUT, channel->Receive(TIMEOUT));
    }

    TEST(ChannelTests, AReplyLongerThanTheCallerTakesIsRefused)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));

        uint8_t request[8] = {10, 20, 30, 40, 50, 60, 70, 80};
        uint8_t reply[4] = {};
        size_t reply_length = 0;

        a.on_block_ = DriverRepliesTooLongThenFits;

        CHECK_EQUAL(CHANNEL_SUCCESS, channel->Call(request, sizeof(request), reply, sizeof(reply), reply_length, TIMEOUT));
        CHECK_EQUAL(CHANNEL_SUCCESS, replied);
        CHECK_EQUAL(4U, reply_length);
        CHECK_EQUAL(11, reply[0]);
        CHECK_EQUAL(41, reply[3]);
    }

    TEST(ChannelTests, TimeoutBeforeTheDriverReceivesWithdrawsTheRequest)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));

        uint8_t request[2] = {1, 2};
        uint8_t reply[2] = {};
        size_t reply_length = 0;

        CHECK_EQUAL(CHANNEL_TIMED_OUT, channel->Call(request, sizeof(request), reply, sizeof(reply), reply_length, TIMEOUT));

        FakeWaitPolicy::current_ = &b;
        CHECK_EQUAL(CHANNEL_TIMED_OUT, channel->Receive(TIMEOUT));

        CHECK_EQUAL(CHANNEL_SUCCESS, EchoCall(2));
    }

    TEST(ChannelTests, ARequestWakesTheWaitingDriver)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));

        FakeWaitPolicy::current_ = &b;
        b.on_block_ = CallerGivesUpBeforeTheDriverRuns;

        //  The caller withdrew its request before the driver ran, so the driver's wait goes on, and times out.

        CHECK_EQUAL(CHANNEL_TIMED_OUT, channel->Receive(TIMEOUT));
        CHECK_EQUAL(CHANNEL_TIMED_OUT, c_called);
        STRCMP_EQUAL("B", FakeWaitPolicy::wake_log_);
    }

    TEST(ChannelTests, AKillEndsTheDriversReceiveAndLeavesTheChannelServed)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));

        FakeWaitPolicy::current_ = &b;
        b.on_block_ = [](FakeTask &blocked) { FakeWaitPolicy::Wake(blocked, task::WaitStatus::INTERRUPTED); };

        CHECK_EQUAL(CHANNEL_INTERRUPTED, channel->Receive(TIMEOUT));
        CHECK_EQUAL(DRIVER, channel->Server());

        CHECK_EQUAL(CHANNEL_SUCCESS, EchoCall(2));
    }

    TEST(ChannelTests, ASignalEndsTheDriversReceiveWithAnEvent)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));

        FakeWaitPolicy::current_ = &b;
        b.on_block_ = [](FakeTask &) { channel->Signal(); };

        CHECK_EQUAL(CHANNEL_EVENT, channel->Receive(TIMEOUT));
        CHECK_EQUAL(1U, b.blocks_);
        STRCMP_EQUAL("B", FakeWaitPolicy::wake_log_);
    }

    TEST(ChannelTests, SignalsMergeUntilAReceiveTakesThem)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));

        channel->Signal();
        channel->Signal();

        FakeWaitPolicy::current_ = &b;

        CHECK_EQUAL(CHANNEL_EVENT, channel->Receive(TIMEOUT));
        CHECK_EQUAL(0U, b.blocks_);
        CHECK_EQUAL(CHANNEL_TIMED_OUT, channel->Receive(TIMEOUT));
    }

    void DriverReceivesWithAnEventPending(FakeTask &)
    {
        FakeWaitPolicy::current_ = &b;

        channel->Signal();
        received = channel->Receive(TIMEOUT);
        replied = channel->Reply(Answer(static_cast<size_t>(received)));
    }

    TEST(ChannelTests, ARequestComesBeforeAPendingEvent)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));

        uint8_t request[2] = {1, 2};
        uint8_t reply[2] = {};
        size_t reply_length = 0;

        a.on_block_ = DriverReceivesWithAnEventPending;

        CHECK_EQUAL(CHANNEL_SUCCESS, channel->Call(request, sizeof(request), reply, sizeof(reply), reply_length, TIMEOUT));
        CHECK_EQUAL(2, received);
        CHECK_EQUAL(CHANNEL_SUCCESS, replied);

        FakeWaitPolicy::current_ = &b;
        CHECK_EQUAL(CHANNEL_EVENT, channel->Receive(TIMEOUT));
    }

    TEST(ChannelTests, AnEventDoesNotOutliveItsDriver)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));
        channel->Signal();
        channel->Unbind(DRIVER);

        CHECK_TRUE(channel->Bind(OTHER_DRIVER));

        FakeWaitPolicy::current_ = &b;
        CHECK_EQUAL(CHANNEL_TIMED_OUT, channel->Receive(TIMEOUT));
    }

    TEST(ChannelTests, LateReplyAfterATimeoutIsDroppedAndTheChannelRecovers)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));

        uint8_t request[2] = {5, 6};
        uint8_t reply[2] = {};
        size_t reply_length = 0;

        a.on_block_ = DriverTakesTheRequest;

        CHECK_EQUAL(CHANNEL_TIMED_OUT, channel->Call(request, sizeof(request), reply, sizeof(reply), reply_length, TIMEOUT));
        CHECK_EQUAL(2, received);
        CHECK_EQUAL(0U, reply_length);

        FakeWaitPolicy::current_ = &b;
        CHECK_EQUAL(CHANNEL_REPLY_PENDING, channel->Receive(TIMEOUT));
        CHECK_EQUAL(CHANNEL_ABANDONED, channel->Reply(Answer(2)));
        CHECK_EQUAL(0, reply[0]);

        CHECK_EQUAL(CHANNEL_SUCCESS, EchoCall(7));
    }

    TEST(ChannelTests, AnAbandonedRequestHoldsTheChannelUntilTheDriverAnswers)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));

        uint8_t request[2] = {5, 6};
        uint8_t reply[2] = {};
        size_t reply_length = 0;

        a.on_block_ = DriverTakesTheRequest;
        CHECK_EQUAL(CHANNEL_TIMED_OUT, channel->Call(request, sizeof(request), reply, sizeof(reply), reply_length, TIMEOUT));

        //  The next call waits its turn, the driver answers the abandoned request, then this one.

        FakeWaitPolicy::current_ = &a;
        a.blocks_ = 0;
        a.on_block_ = DriverAnswersTheAbandonedRequest;

        CHECK_EQUAL(CHANNEL_SUCCESS, channel->Call(request, sizeof(request), reply, sizeof(reply), reply_length, TIMEOUT));
        CHECK_EQUAL(2U, reply_length);
        CHECK_EQUAL(6, reply[0]);
        CHECK_EQUAL(7, reply[1]);
        CHECK_EQUAL(2U, a.blocks_);
    }

    TEST(ChannelTests, AReplyThatRacesTheTimeoutStillCounts)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));

        uint8_t request[3] = {1, 2, 3};
        uint8_t reply[3] = {};
        size_t reply_length = 0;

        a.on_block_ = DriverRepliesAsTheTimeoutPasses;

        CHECK_EQUAL(CHANNEL_SUCCESS, channel->Call(request, sizeof(request), reply, sizeof(reply), reply_length, TIMEOUT));
        CHECK_EQUAL(3U, reply_length);
        CHECK_EQUAL(4, reply[2]);
    }

    TEST(ChannelTests, CallersTakeTurnsEachWithinItsOwnTimeout)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));

        uint8_t request[2] = {1, 2};
        uint8_t reply[2] = {};
        size_t reply_length = 0;

        a.on_block_ = SecondCallerTimesOutThenDriverEchoes;

        CHECK_EQUAL(CHANNEL_SUCCESS, channel->Call(request, sizeof(request), reply, sizeof(reply), reply_length, TIMEOUT));
        CHECK_EQUAL(CHANNEL_TIMED_OUT, c_called);
        CHECK_EQUAL(1U, c.blocks_);
        CHECK_EQUAL(2, reply[0]);
        CHECK_EQUAL(3, reply[1]);

        CHECK_EQUAL(CHANNEL_SUCCESS, EchoCall(1));
    }

    TEST(ChannelTests, DriverExitFailsTheCallItHeld)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));

        uint8_t request[2] = {1, 2};
        uint8_t reply[2] = {};
        size_t reply_length = 0;

        a.on_block_ = DriverExitsHoldingTheRequest;

        CHECK_EQUAL(CHANNEL_SERVER_EXITED, channel->Call(request, sizeof(request), reply, sizeof(reply), reply_length, TIMEOUT));
        CHECK_EQUAL(2, received);
        CHECK_EQUAL(0U, reply_length);

        CHECK_EQUAL(CHANNEL_NO_SERVER, channel->Call(request, sizeof(request), reply, sizeof(reply), reply_length, TIMEOUT));
    }

    TEST(ChannelTests, DriverExitBeforeReceivingFailsTheCall)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));

        uint8_t request[2] = {1, 2};
        uint8_t reply[2] = {};
        size_t reply_length = 0;

        a.on_block_ = DriverExitsBeforeReceiving;

        CHECK_EQUAL(CHANNEL_SERVER_EXITED, channel->Call(request, sizeof(request), reply, sizeof(reply), reply_length, TIMEOUT));
    }

    TEST(ChannelTests, ARestartedDriverServesTheNextCall)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));

        uint8_t request[2] = {1, 2};
        uint8_t reply[2] = {};
        size_t reply_length = 0;

        //  Exiting with an abandoned request frees the channel; exiting with one in flight fails it.

        a.on_block_ = DriverTakesTheRequest;
        CHECK_EQUAL(CHANNEL_TIMED_OUT, channel->Call(request, sizeof(request), reply, sizeof(reply), reply_length, TIMEOUT));

        channel->Unbind(DRIVER);
        CHECK_TRUE(channel->Bind(OTHER_DRIVER));

        CHECK_EQUAL(CHANNEL_SUCCESS, EchoCall(2));

        FakeWaitPolicy::current_ = &a;
        a.on_block_ = DriverExitsHoldingTheRequest;
        channel->Unbind(OTHER_DRIVER);
        CHECK_TRUE(channel->Bind(DRIVER));

        CHECK_EQUAL(CHANNEL_SERVER_EXITED, channel->Call(request, sizeof(request), reply, sizeof(reply), reply_length, TIMEOUT));
        CHECK_TRUE(channel->Bind(OTHER_DRIVER));

        CHECK_EQUAL(CHANNEL_SUCCESS, EchoCall(1));
    }

    TEST(ChannelTests, DataTravelsBothWaysBesideTheMessages)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));
        CHECK_EQUAL(DATA_SIZE, channel->DataSize());

        static uint8_t out[DATA_SIZE];
        static uint8_t in[DATA_SIZE];
        uint8_t request[2] = {1, 2};
        uint8_t reply[2] = {};

        for (size_t i = 0; i < DATA_SIZE; i++)
        {
            out[i] = static_cast<uint8_t>(i * 7);
            in[i] = 0;
        }

        const size_t lengths[] = {1, 512, DATA_SIZE};

        for (size_t length : lengths)
        {
            task::ChannelCall call;

            call.request = request;
            call.request_length = sizeof(request);
            call.reply = reply;
            call.reply_capacity = sizeof(reply);
            call.data_out = out;
            call.data_out_length = length;
            call.data_in = in;
            call.data_in_capacity = sizeof(in);

            FakeWaitPolicy::current_ = &a;
            a.on_block_ = DriverComplementsTheData;

            CHECK_EQUAL(CHANNEL_SUCCESS, channel->Call(call, TIMEOUT));
            CHECK_EQUAL(2, received);
            CHECK_EQUAL(length, data_received);
            CHECK_EQUAL(1U, call.reply_length);
            CHECK_EQUAL(0x5A, reply[0]);
            CHECK_EQUAL(length, call.data_in_length);
            CHECK_EQUAL(static_cast<uint8_t>(~out[0]), in[0]);
            CHECK_EQUAL(static_cast<uint8_t>(~out[length - 1]), in[length - 1]);
        }

        //  A call with messages only tells the driver it carries no data.

        CHECK_EQUAL(CHANNEL_SUCCESS, EchoCall(3));

        FakeWaitPolicy::current_ = &a;
        a.on_block_ = DriverComplementsTheData;

        size_t reply_length = 0;

        CHECK_EQUAL(CHANNEL_SUCCESS, channel->Call(request, sizeof(request), reply, sizeof(reply), reply_length, TIMEOUT));
        CHECK_EQUAL(0U, data_received);
    }

    TEST(ChannelTests, DataBeyondTheAreaOrTheCallersBufferIsRefused)
    {
        Channel under_test;
        Reset(under_test);

        CHECK_TRUE(channel->Bind(DRIVER));

        static uint8_t out[DATA_SIZE + 1];
        uint8_t in[16] = {};
        uint8_t request[1] = {1};

        task::ChannelCall call;

        call.request = request;
        call.request_length = sizeof(request);
        call.data_out = out;
        call.data_out_length = sizeof(out);

        CHECK_EQUAL(CHANNEL_TOO_LONG, channel->Call(call, TIMEOUT));
        CHECK_EQUAL(0U, a.blocks_);

        call.data_out_length = 0;
        call.data_in = in;
        call.data_in_capacity = sizeof(in);
        a.on_block_ = DriverSendsTooMuchDataThenFits;

        CHECK_EQUAL(CHANNEL_SUCCESS, channel->Call(call, TIMEOUT));
        CHECK_EQUAL(CHANNEL_SUCCESS, replied);
        CHECK_EQUAL(16U, call.data_in_length);
        CHECK_EQUAL(0x33, in[15]);
    }

    TEST(ChannelTests, AChannelWithNoDataAreaCarriesMessagesOnly)
    {
        Channel under_test;
        Reset(under_test);

        channel->SetMemory(page, 0);
        CHECK_TRUE(channel->Bind(DRIVER));

        uint8_t out[1] = {1};
        uint8_t request[1] = {1};

        task::ChannelCall call;

        call.request = request;
        call.request_length = sizeof(request);
        call.data_out = out;
        call.data_out_length = sizeof(out);

        CHECK_EQUAL(CHANNEL_TOO_LONG, channel->Call(call, TIMEOUT));
        CHECK_EQUAL(CHANNEL_SUCCESS, EchoCall(1));

        FakeWaitPolicy::current_ = &b;
        CHECK_EQUAL(CHANNEL_TOO_LONG, channel->Reply(0, 1));
    }
}
