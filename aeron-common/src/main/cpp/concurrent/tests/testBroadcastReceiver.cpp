/*
 * Copyright 2014 Real Logic Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <array>

#include <gtest/gtest.h>
#include <mintomic/mintomic.h>

#include <thread>
#include "MockAtomicBuffer.h"
#include <concurrent/broadcast/BroadcastBufferDescriptor.h>
#include <concurrent/broadcast/BroadcastReceiver.h>

using namespace aeron::common::concurrent::broadcast;
using namespace aeron::common::concurrent::mock;
using namespace aeron::common::concurrent;
using namespace aeron::common;

#define CAPACITY (1024)
#define TOTAL_BUFFER_SIZE (CAPACITY + BroadcastBufferDescriptor::TRAILER_LENGTH)
#define MSG_TYPE_ID (7)
#define TAIL_COUNTER_INDEX (CAPACITY + BroadcastBufferDescriptor::TAIL_COUNTER_OFFSET)
#define LATEST_COUNTER_INDEX (CAPACITY + BroadcastBufferDescriptor::LATEST_COUNTER_OFFSET)

typedef std::array<std::uint8_t, TOTAL_BUFFER_SIZE> buffer_t;

class BroadcastReceiverTest : public testing::Test
{
public:
    BroadcastReceiverTest() :
        m_mockBuffer(&m_buffer[0], m_buffer.size()),
        m_broadcastReceiver(m_mockBuffer)
    {
        m_buffer.fill(0);
    }

    virtual void SetUp()
    {
        m_buffer.fill(0);
    }

protected:
    MINT_DECL_ALIGNED(buffer_t m_buffer, 16);
    MockAtomicBuffer m_mockBuffer;
    BroadcastReceiver m_broadcastReceiver;
};

TEST_F(BroadcastReceiverTest, shouldCalculateCapacityForBuffer)
{
    EXPECT_EQ(m_broadcastReceiver.capacity(), CAPACITY);
}

TEST_F(BroadcastReceiverTest, shouldThrowExceptionForCapacityThatIsNotPowerOfTwo)
{
    typedef std::array<std::uint8_t, (777 + BroadcastBufferDescriptor::TRAILER_LENGTH)> non_power_of_two_buffer_t;
    MINT_DECL_ALIGNED(non_power_of_two_buffer_t non_power_of_two_buffer, 16);
    AtomicBuffer buffer(&non_power_of_two_buffer[0], non_power_of_two_buffer.size());

    ASSERT_THROW(
    {
        BroadcastReceiver receiver(buffer);
    }, util::IllegalStateException);
}

TEST_F(BroadcastReceiverTest, shouldNotBeLappedBeforeReception)
{
    EXPECT_EQ(m_broadcastReceiver.lappedCount(), 0);
}

TEST_F(BroadcastReceiverTest, shouldNotReceiveFromEmptyBuffer)
{
    EXPECT_CALL(m_mockBuffer, getInt64Ordered(TAIL_COUNTER_INDEX))
        .Times(1)
        .WillOnce(testing::Return(0));

    EXPECT_FALSE(m_broadcastReceiver.receiveNext());
}

TEST_F(BroadcastReceiverTest, shouldReceiveFirstMessageFromBuffer)
{
    const std::int32_t length = 8;
    const std::int32_t recordLength = util::BitUtil::align(length + RecordDescriptor::HEADER_LENGTH, RecordDescriptor::RECORD_ALIGNMENT);
    const std::int64_t tail = recordLength;
    const std::int64_t latestRecord = tail - recordLength;
    const std::int32_t recordOffset = (std::int32_t)latestRecord;
    testing::Sequence sequence;

    EXPECT_CALL(m_mockBuffer, getInt64Ordered(TAIL_COUNTER_INDEX))
        .Times(1)
        .InSequence(sequence)
        .WillOnce(testing::Return(tail));
    EXPECT_CALL(m_mockBuffer, getInt64Ordered(LATEST_COUNTER_INDEX))
        .Times(0);
    EXPECT_CALL(m_mockBuffer, getInt64Ordered(RecordDescriptor::tailSequenceOffset(recordOffset)))
        .Times(2)
        .InSequence(sequence)
        .WillRepeatedly(testing::Return(latestRecord));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::recLengthOffset(recordOffset)))
        .Times(1)
        .WillOnce(testing::Return(recordLength));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::msgLengthOffset(recordOffset)))
        .Times(1)
        .WillOnce(testing::Return(length));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::msgTypeOffset(recordOffset)))
        .Times(2)
        .WillRepeatedly(testing::Return(MSG_TYPE_ID));

    EXPECT_TRUE(m_broadcastReceiver.receiveNext());
    EXPECT_EQ(m_broadcastReceiver.typeId(), MSG_TYPE_ID);
    EXPECT_EQ(&(m_broadcastReceiver.buffer()), &m_mockBuffer);
    EXPECT_EQ(m_broadcastReceiver.offset(), RecordDescriptor::msgOffset(recordOffset));
    EXPECT_EQ(m_broadcastReceiver.length(), length);
    EXPECT_TRUE(m_broadcastReceiver.validate());
}

TEST_F(BroadcastReceiverTest, shouldReceiveTwoMessagesFromBuffer)
{
    const std::int32_t length = 8;
    const std::int32_t recordLength = util::BitUtil::align(length + RecordDescriptor::HEADER_LENGTH, RecordDescriptor::RECORD_ALIGNMENT);
    const std::int64_t tail = recordLength * 2;
    const std::int64_t latestRecord = tail - recordLength;
    const std::int32_t recordOffsetOne = 0;
    const std::int32_t recordOffsetTwo = (std::int32_t)latestRecord;

    EXPECT_CALL(m_mockBuffer, getInt64Ordered(TAIL_COUNTER_INDEX))
        .Times(2)
        .WillRepeatedly(testing::Return(tail));
    EXPECT_CALL(m_mockBuffer, getInt64Ordered(LATEST_COUNTER_INDEX))
        .Times(0);

    EXPECT_CALL(m_mockBuffer, getInt64Ordered(RecordDescriptor::tailSequenceOffset(recordOffsetOne)))
        .Times(2)
        .WillRepeatedly(testing::Return(0));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::recLengthOffset(recordOffsetOne)))
        .Times(1)
        .WillOnce(testing::Return(recordLength));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::msgLengthOffset(recordOffsetOne)))
        .Times(1)
        .WillOnce(testing::Return(length));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::msgTypeOffset(recordOffsetOne)))
        .Times(2)
        .WillRepeatedly(testing::Return(MSG_TYPE_ID));

    EXPECT_CALL(m_mockBuffer, getInt64Ordered(RecordDescriptor::tailSequenceOffset(recordOffsetTwo)))
        .Times(2)
        .WillRepeatedly(testing::Return(latestRecord));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::recLengthOffset(recordOffsetTwo)))
        .Times(1)
        .WillOnce(testing::Return(recordLength));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::msgLengthOffset(recordOffsetTwo)))
        .Times(1)
        .WillOnce(testing::Return(length));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::msgTypeOffset(recordOffsetTwo)))
        .Times(2)
        .WillRepeatedly(testing::Return(MSG_TYPE_ID));

    EXPECT_TRUE(m_broadcastReceiver.receiveNext());
    EXPECT_EQ(m_broadcastReceiver.typeId(), MSG_TYPE_ID);
    EXPECT_EQ(&(m_broadcastReceiver.buffer()), &m_mockBuffer);
    EXPECT_EQ(m_broadcastReceiver.offset(), RecordDescriptor::msgOffset(recordOffsetOne));
    EXPECT_EQ(m_broadcastReceiver.length(), length);

    EXPECT_TRUE(m_broadcastReceiver.validate());

    EXPECT_TRUE(m_broadcastReceiver.receiveNext());
    EXPECT_EQ(m_broadcastReceiver.typeId(), MSG_TYPE_ID);
    EXPECT_EQ(&(m_broadcastReceiver.buffer()), &m_mockBuffer);
    EXPECT_EQ(m_broadcastReceiver.offset(), RecordDescriptor::msgOffset(recordOffsetTwo));
    EXPECT_EQ(m_broadcastReceiver.length(), length);

    EXPECT_TRUE(m_broadcastReceiver.validate());
}

TEST_F(BroadcastReceiverTest, shouldLateJoinTransmission)
{
    const std::int32_t length = 8;
    const std::int32_t recordLength = util::BitUtil::align(length + RecordDescriptor::HEADER_LENGTH, RecordDescriptor::RECORD_ALIGNMENT);
    const std::int64_t tail = CAPACITY * 3 + RecordDescriptor::RECORD_ALIGNMENT + recordLength;
    const std::int64_t latestRecord = tail - recordLength;
    const std::int32_t recordOffset = (std::int32_t)latestRecord & (CAPACITY - 1);

    EXPECT_CALL(m_mockBuffer, getInt64Ordered(TAIL_COUNTER_INDEX))
        .Times(1)
        .WillOnce(testing::Return(tail));
    EXPECT_CALL(m_mockBuffer, getInt64Ordered(LATEST_COUNTER_INDEX))
        .Times(1)
        .WillOnce(testing::Return(latestRecord));

    EXPECT_CALL(m_mockBuffer, getInt64Ordered(RecordDescriptor::tailSequenceOffset(0)))
        .Times(1)
        .WillOnce(testing::Return(CAPACITY * 3));
    EXPECT_CALL(m_mockBuffer, getInt64Ordered(RecordDescriptor::tailSequenceOffset(recordOffset)))
        .Times(1)
        .WillOnce(testing::Return(latestRecord));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::recLengthOffset(recordOffset)))
        .Times(1)
        .WillOnce(testing::Return(recordLength));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::msgLengthOffset(recordOffset)))
        .Times(1)
        .WillOnce(testing::Return(length));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::msgTypeOffset(recordOffset)))
        .Times(2)
        .WillRepeatedly(testing::Return(MSG_TYPE_ID));

    EXPECT_TRUE(m_broadcastReceiver.receiveNext());
    EXPECT_EQ(m_broadcastReceiver.typeId(), MSG_TYPE_ID);
    EXPECT_EQ(&(m_broadcastReceiver.buffer()), &m_mockBuffer);
    EXPECT_EQ(m_broadcastReceiver.offset(), RecordDescriptor::msgOffset(recordOffset));
    EXPECT_EQ(m_broadcastReceiver.length(), length);
    EXPECT_TRUE(m_broadcastReceiver.validate());
    EXPECT_GT(m_broadcastReceiver.lappedCount(), 0);
}

TEST_F(BroadcastReceiverTest, shouldCopeWithPaddingRecordAndWrapOfBufferToNextRecord)
{
    const std::int32_t length = 120;
    const std::int32_t recordLength = util::BitUtil::align(length + RecordDescriptor::HEADER_LENGTH, RecordDescriptor::RECORD_ALIGNMENT);
    const std::int64_t catchupTail = (CAPACITY * 2) - RecordDescriptor::RECORD_ALIGNMENT;
    const std::int64_t postPaddingTail = catchupTail + RecordDescriptor::RECORD_ALIGNMENT + recordLength;
    const std::int64_t latestRecord = catchupTail - recordLength;
    const std::int32_t catchupOffset = (std::int32_t)latestRecord & (CAPACITY - 1);
    testing::Sequence sequence;

    EXPECT_CALL(m_mockBuffer, getInt64Ordered(TAIL_COUNTER_INDEX))
        .Times(2)
        .WillOnce(testing::Return(catchupTail))
        .WillOnce(testing::Return(postPaddingTail));
    EXPECT_CALL(m_mockBuffer, getInt64Ordered(LATEST_COUNTER_INDEX))
        .Times(1)
        .WillOnce(testing::Return(latestRecord));

    EXPECT_CALL(m_mockBuffer, getInt64Ordered(RecordDescriptor::tailSequenceOffset(0)))
        .Times(1)
        .InSequence(sequence)
        .WillOnce(testing::Return(CAPACITY * 2));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::recLengthOffset(catchupOffset)))
        .Times(1)
        .WillOnce(testing::Return(recordLength));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::msgTypeOffset(catchupOffset)))
        .Times(1)
        .WillOnce(testing::Return(MSG_TYPE_ID));

    const std::int32_t paddingOffset = (std::int32_t)catchupTail & (CAPACITY - 1);
    const std::int32_t recordOffset = (std::int32_t)(postPaddingTail - recordLength) & (CAPACITY - 1);

    EXPECT_CALL(m_mockBuffer, getInt64Ordered(RecordDescriptor::tailSequenceOffset(paddingOffset)))
        .Times(1)
        .InSequence(sequence)
        .WillOnce(testing::Return(catchupTail));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::recLengthOffset(paddingOffset)))
        .Times(1)
        .WillOnce(testing::Return(RecordDescriptor::RECORD_ALIGNMENT));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::msgTypeOffset(paddingOffset)))
        .Times(1)
        .WillOnce(testing::Return(RecordDescriptor::PADDING_MSG_TYPE_ID));

    EXPECT_CALL(m_mockBuffer, getInt64Ordered(RecordDescriptor::tailSequenceOffset(recordOffset)))
        .Times(1)
        .InSequence(sequence)
        .WillOnce(testing::Return(postPaddingTail - recordLength));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::recLengthOffset(recordOffset)))
        .Times(1)
        .WillOnce(testing::Return(recordLength));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::msgLengthOffset(recordOffset)))
        .Times(1)
        .WillOnce(testing::Return(length));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::msgTypeOffset(recordOffset)))
        .Times(1)
        .WillOnce(testing::Return(MSG_TYPE_ID));

    EXPECT_TRUE(m_broadcastReceiver.receiveNext());
    EXPECT_TRUE(m_broadcastReceiver.receiveNext());
    EXPECT_EQ(m_broadcastReceiver.typeId(), MSG_TYPE_ID);
    EXPECT_EQ(&(m_broadcastReceiver.buffer()), &m_mockBuffer);
    EXPECT_EQ(m_broadcastReceiver.offset(), RecordDescriptor::msgOffset(recordOffset));
    EXPECT_EQ(m_broadcastReceiver.length(), length);
    EXPECT_TRUE(m_broadcastReceiver.validate());
}

TEST_F(BroadcastReceiverTest, shouldDealWithRecordBecomingInvalidDueToOverwrite)
{
    const std::int32_t length = 8;
    const std::int32_t recordLength = util::BitUtil::align(length + RecordDescriptor::HEADER_LENGTH, RecordDescriptor::RECORD_ALIGNMENT);
    const std::int64_t tail = recordLength;
    const std::int64_t latestRecord = tail - recordLength;
    const std::int32_t recordOffset = (std::int32_t)latestRecord;
    testing::Sequence sequence;

    EXPECT_CALL(m_mockBuffer, getInt64Ordered(TAIL_COUNTER_INDEX))
        .Times(1)
        .InSequence(sequence)
        .WillOnce(testing::Return(tail));
    EXPECT_CALL(m_mockBuffer, getInt64Ordered(LATEST_COUNTER_INDEX))
        .Times(0);
    EXPECT_CALL(m_mockBuffer, getInt64Ordered(RecordDescriptor::tailSequenceOffset(recordOffset)))
        .Times(2)
        .InSequence(sequence)
        .WillOnce(testing::Return(latestRecord))
        .WillOnce(testing::Return(latestRecord + CAPACITY));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::recLengthOffset(recordOffset)))
        .Times(1)
        .WillOnce(testing::Return(recordLength));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::msgLengthOffset(recordOffset)))
        .Times(1)
        .WillOnce(testing::Return(length));
    EXPECT_CALL(m_mockBuffer, getInt32(RecordDescriptor::msgTypeOffset(recordOffset)))
        .Times(2)
        .WillRepeatedly(testing::Return(MSG_TYPE_ID));

    EXPECT_TRUE(m_broadcastReceiver.receiveNext());
    EXPECT_EQ(m_broadcastReceiver.typeId(), MSG_TYPE_ID);
    EXPECT_EQ(&(m_broadcastReceiver.buffer()), &m_mockBuffer);
    EXPECT_EQ(m_broadcastReceiver.offset(), RecordDescriptor::msgOffset(recordOffset));
    EXPECT_EQ(m_broadcastReceiver.length(), length);
    EXPECT_FALSE(m_broadcastReceiver.validate());
}