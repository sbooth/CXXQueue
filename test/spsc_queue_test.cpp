//
// SPDX-FileCopyrightText: 2026 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXQueue
//

#include "spsc/Queue.hpp"

#include <gtest/gtest.h>

namespace spsc::test {

// ============================================================================
// 1. Concept & Compile-Time Constraint Verification
// ============================================================================
TEST(QueueStaticTest, ConceptConstraints) {
    // Test ValidPowerOfTwo
    static_assert(ValidPowerOfTwo<2>);
    static_assert(ValidPowerOfTwo<4>);
    static_assert(ValidPowerOfTwo<1024>);
    static_assert(!ValidPowerOfTwo<0>);
    static_assert(!ValidPowerOfTwo<1>);
    static_assert(!ValidPowerOfTwo<3>);
    static_assert(!ValidPowerOfTwo<1000>);
}

// ============================================================================
// 2. Initial State & Configuration Tests
// ============================================================================
TEST(QueueTest, InitialStateAndAttributes) {
    constexpr std::size_t N = 4;
    Queue<int, N> queue;

    EXPECT_EQ(decltype(queue)::size, N);

    // Consumer-safe checks on empty queue
    EXPECT_TRUE(queue.isEmpty());
    EXPECT_EQ(queue.count(), 0);

    // Producer-safe checks on empty queue
    EXPECT_FALSE(queue.isFull());
    EXPECT_EQ(queue.free(), N);
}

} /* namespace spsc::test */
