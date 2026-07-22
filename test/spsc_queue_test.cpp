//
// SPDX-FileCopyrightText: 2026 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXQueue
//

#include "spsc/Queue.hpp"

#include <numeric>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

class QueueTest : public ::testing::Test {
  protected:
    static constexpr std::size_t kCapacity = 4;
    spsc::Queue<int, kCapacity> queue_;
};

// MARK: - Core Operations

TEST_F(QueueTest, PushAndPopSingleElement) {
    int val = 0;

    EXPECT_TRUE(queue_.push(42));
    EXPECT_FALSE(queue_.isEmpty());
    EXPECT_EQ(queue_.availableToRead(), 1);

    EXPECT_TRUE(queue_.peek(val));
    EXPECT_EQ(val, 42);

    EXPECT_TRUE(queue_.pop(val));
    EXPECT_EQ(val, 42);
    EXPECT_TRUE(queue_.isEmpty());
}

TEST_F(QueueTest, PushUntilFullAndPopUntilEmpty) {
    // Fill the queue
    for (int i = 0; i < static_cast<int>(kCapacity); ++i) {
        EXPECT_TRUE(queue_.push(i));
    }

    EXPECT_TRUE(queue_.isFull());
    EXPECT_EQ(queue_.availableToWrite(), 0);
    EXPECT_FALSE(queue_.push(999)); // Should fail when full

    // Empty the queue
    int val = 0;
    for (int i = 0; i < static_cast<int>(kCapacity); ++i) {
        EXPECT_TRUE(queue_.pop(val));
        EXPECT_EQ(val, i);
    }

    EXPECT_TRUE(queue_.isEmpty());
    EXPECT_FALSE(queue_.pop(val)); // Should fail when empty
}

// MARK: - Discard

TEST_F(QueueTest, DiscardElements) {
    for (int i = 0; i < 4; ++i) {
        queue_.push(i);
    }

    // Discard a subset
    EXPECT_EQ(queue_.discard(2), 2);
    EXPECT_EQ(queue_.availableToRead(), 2);

    int val = 0;
    EXPECT_TRUE(queue_.pop(val));
    EXPECT_EQ(val, 2);

    // Try to discard more than available
    EXPECT_EQ(queue_.discard(5), 1);
    EXPECT_TRUE(queue_.isEmpty());
}

TEST_F(QueueTest, DiscardAllElements) {
    for (int i = 0; i < 3; ++i) {
        queue_.push(i);
    }

    EXPECT_EQ(queue_.discardAll(), 3);
    EXPECT_TRUE(queue_.isEmpty());
    EXPECT_EQ(queue_.availableToRead(), 0);
    EXPECT_EQ(queue_.availableToWrite(), kCapacity);
}

// MARK: - Ring Wrap-Around Checks

TEST_F(QueueTest, WrapAroundBehavior) {
    int val = 0;

    // Push and pop repeatedly to cycle through free-running indices
    for (int cycle = 0; cycle < 10; ++cycle) {
        EXPECT_TRUE(queue_.push(cycle));
        EXPECT_TRUE(queue_.push(cycle + 10));

        EXPECT_TRUE(queue_.pop(val));
        EXPECT_EQ(val, cycle);
        EXPECT_TRUE(queue_.pop(val));
        EXPECT_EQ(val, cycle + 10);
    }

    // Check sequence alignment tracking
    EXPECT_EQ(queue_.writePosition(), 20);
    EXPECT_EQ(queue_.readPosition(), 20);
}

// MARK: - Advanced Transactional APIs

TEST_F(QueueTest, WriteAndReadTransaction) {
    // Nothing to read when empty
    auto readTrans1 = queue_.beginRead();
    EXPECT_EQ(readTrans1.availableToRead(), 0);
    ASSERT_FALSE(readTrans1.commit(0));

    EXPECT_TRUE(queue_.isEmpty());

    // Fill
    auto writeTrans1 = queue_.beginWrite();
    EXPECT_EQ(writeTrans1.availableToWrite(), 4);
    writeTrans1.first[0] = 100;
    writeTrans1.first[1] = 101;
    writeTrans1.first[2] = 102;
    writeTrans1.first[3] = 103;

    // Ensure erroneous and double commits fail
    EXPECT_FALSE(writeTrans1.commit(5));
    EXPECT_EQ(writeTrans1.availableToWrite(), 4);
    EXPECT_TRUE(writeTrans1.commit(4));
    EXPECT_EQ(writeTrans1.availableToWrite(), 0);
    EXPECT_FALSE(writeTrans1.commit(4));

    EXPECT_TRUE(queue_.isFull());

    auto writeTrans2 = queue_.beginWrite();
    EXPECT_EQ(writeTrans2.availableToWrite(), 0);
    ASSERT_FALSE(writeTrans2.commit(0));

    auto readTrans2 = queue_.beginRead();
    EXPECT_EQ(readTrans2.availableToRead(), 4);
    ASSERT_FALSE(readTrans2.commit(5));
    ASSERT_TRUE(readTrans2.commit(2));
    ASSERT_FALSE(readTrans2.commit(1));

    EXPECT_EQ(queue_.availableToRead(), 2);
}

TEST_F(QueueTest, WriteAndReadTransactionContiguous) {
    // When empty, beginWrite should return a contiguous span to the end of the buffer
    auto writeTrans = queue_.beginWrite();
    ASSERT_EQ(writeTrans.first.size(), kCapacity);
    ASSERT_EQ(writeTrans.second.size(), 0);

    // Stage writing 2 elements manually
    writeTrans.first[0] = 100;
    writeTrans.first[1] = 201;
    EXPECT_TRUE(writeTrans.commit(2));

    EXPECT_EQ(queue_.availableToRead(), 2);

    // Read transaction should expose these 2 contiguous elements
    auto readTrans = queue_.beginRead();
    ASSERT_EQ(readTrans.first.size(), 2);
    ASSERT_EQ(readTrans.second.size(), 0);
    EXPECT_EQ(readTrans.first[0], 100);
    EXPECT_EQ(readTrans.first[1], 201);

    EXPECT_TRUE(readTrans.commit(2));
    EXPECT_TRUE(queue_.isEmpty());
}

TEST_F(QueueTest, TransactionWrapAroundSplitting) {
    // 1. Move the read/write pointers to an offset near the end.
    // Push 3 elements (indices 0, 1, 2 used).
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(queue_.push(10 * i));
    }
    // Pop 2 elements. Read index is now 2, Write index is still 3.
    int dummy;
    ASSERT_TRUE(queue_.pop(dummy));
    ASSERT_TRUE(queue_.pop(dummy));

    // Current layout:
    // Index 0: (Empty)
    // Index 1: (Empty)
    // Index 2: Valid data (Value: 20) <-- Read Position
    // Index 3: (Empty)                <-- Write Position

    // 2. Test WRITE transaction splitting.
    // There are 3 free slots (Index 3, Index 0, Index 1).
    // writeIndex is 3. toEnd is 4 - 3 = 1.
    // Because free (3) > toEnd (1), it must split!
    auto writeTrans = queue_.beginWrite();
    ASSERT_EQ(writeTrans.first.size(), 1);  // Index 3
    ASSERT_EQ(writeTrans.second.size(), 2); // Indices 0 and 1

    // Stage data into the write spans
    writeTrans.first[0] = 30;         // Goes into index 3
    writeTrans.second[0] = 40;        // Goes into index 0
    writeTrans.second[1] = 50;        // Goes into index 1
    EXPECT_TRUE(writeTrans.commit(3)); // Write position is now 6 (6 & 3 = 2)

    // 3. Test READ transaction splitting.
    // Current layout:
    // Index 0: 40
    // Index 1: 50
    // Index 2: 20 <-- Read Position
    // Index 3: 30
    // Total used slots = 4 (Full queue). Read position is 2. toEnd is 4 - 2 = 2.
    // Because used (4) > toEnd (2), it must split!
    auto readTrans = queue_.beginRead();
    ASSERT_EQ(readTrans.first.size(), 2);  // Indices 2 and 3
    ASSERT_EQ(readTrans.second.size(), 2); // Indices 0 and 1

    EXPECT_EQ(readTrans.first[0], 20);
    EXPECT_EQ(readTrans.first[1], 30);
    EXPECT_EQ(readTrans.second[0], 40);
    EXPECT_EQ(readTrans.second[1], 50);

    EXPECT_TRUE(readTrans.commit(4));
    EXPECT_TRUE(queue_.isEmpty());
}

// MARK: - Thread-Safety Validation

TEST(QueueConcurrencyTest, SingleProducerSingleConsumer) {
    constexpr std::size_t N = 1024;
    spsc::Queue<int, N> concurrent_queue;
    constexpr int kTotalElements = 50000;

    std::thread producer([&]() {
        for (int i = 0; i < kTotalElements; ++i) {
            while (!concurrent_queue.push(i)) {
                std::this_thread::yield(); // Backoff if full
            }
        }
    });

    std::vector<int> received_data;
    received_data.reserve(kTotalElements);

    std::thread consumer([&]() {
        int value = 0;
        while (received_data.size() < kTotalElements) {
            if (concurrent_queue.pop(value)) {
                received_data.push_back(value);
            } else {
                std::this_thread::yield(); // Backoff if empty
            }
        }
    });

    producer.join();
    consumer.join();

    // Verify all data was transferred linearly without corruptions
    ASSERT_EQ(received_data.size(), kTotalElements);
    for (int i = 0; i < kTotalElements; ++i) {
        ASSERT_EQ(received_data[i], i);
    }
}
