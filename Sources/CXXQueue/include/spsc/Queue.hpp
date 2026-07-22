//
// SPDX-FileCopyrightText: 2026 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXQueue
//

#ifndef SPSC_QUEUE_HPP
#define SPSC_QUEUE_HPP

#include <algorithm>
#include <atomic>
#include <bit>
#include <concepts>
#include <new>
#include <span>
#include <type_traits>

namespace spsc {

template <std::size_t N>
concept ValidPowerOfTwo = (N >= 2) && std::has_single_bit(N);

template <typename T>
concept ValueLike = std::is_object_v<std::remove_cvref_t<T>> && std::is_trivially_copyable_v<std::remove_cvref_t<T>> &&
                    std::is_standard_layout_v<std::remove_cvref_t<T>> && !std::is_pointer_v<std::remove_cvref_t<T>> &&
                    !std::ranges::range<std::remove_cvref_t<T>>;

/// A lock-free SPSC queue.
///
/// This class is thread safe when used with a single producer and a single consumer.
///
/// @note The queue is only safe if exactly one thread performs producer operations and exactly one thread performs
/// consumer operations. Calling producer APIs concurrently from multiple threads results in undefined behavior.
template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
class Queue final {
  public:
    /// Unsigned integer type.
    using SizeType = std::size_t;
    /// Atomic unsigned integer type.
    using AtomicSizeType = std::atomic<SizeType>;

    /// A write vector.
    using WriteVector = std::pair<std::span<T>, std::span<T>>;
    /// A read vector.
    using ReadVector = std::pair<std::span<const T>, std::span<const T>>;

    // MARK: Construction and Destruction

    /// Creates an empty queue.
    Queue() noexcept = default;

    Queue(const Queue &) = delete;
    Queue &operator=(const Queue &) = delete;
    Queue(Queue &&) noexcept = delete;
    Queue &operator=(Queue &&) noexcept = delete;

    /// Destroys the queue and releases all associated resources.
    ~Queue() noexcept = default;

    // MARK: Information

    /// The capacity of the queue.
    static constexpr auto capacity = N;

    /// Returns the current write position in the queue.
    /// @note The result of this method is only accurate when called from the producer.
    /// @return The current write position.
    [[nodiscard]] SizeType writePosition() const noexcept [[clang::nonblocking]];

    /// Returns the current read position in the queue.
    /// @note The result of this method is only accurate when called from the consumer.
    /// @return The current read position.
    [[nodiscard]] SizeType readPosition() const noexcept [[clang::nonblocking]];

    // MARK: Statistics

    /// Returns true if the queue is full.
    /// @note The result of this method is only accurate when called from the producer.
    /// @return true if the queue is full.
    [[nodiscard]] bool isFull() const noexcept [[clang::nonblocking]];

    /// Returns true if the queue is empty.
    /// @note The result of this method is only accurate when called from the consumer.
    /// @return true if the queue contains no values.
    [[nodiscard]] bool isEmpty() const noexcept [[clang::nonblocking]];

    /// Returns the number of vacant slots in the queue.
    /// @note The result of this method is only accurate when called from the producer.
    /// @return The number of unoccupied slots available for writing.
    [[nodiscard]] SizeType free() const noexcept [[clang::nonblocking]];

    /// Returns the number of occupied slots in the queue.
    /// @note The result of this method is only accurate when called from the consumer.
    /// @return The number of occupied slots available for reading.
    [[nodiscard]] SizeType size() const noexcept [[clang::nonblocking]];

    // MARK: Queue Operations

    /// Copies a value to the next vacant slot and advances the write position.
    /// @note This method is only safe to call from the producer.
    /// @param value The value to copy.
    /// @return false if the queue is full.
    [[nodiscard]] bool push(const T &value) noexcept [[clang::nonblocking]];

    /// Copies a value from the first occupied slot and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @param value A reference to receive the value.
    /// @return false if the queue is empty.
    [[nodiscard]] bool pop(T &value) noexcept [[clang::nonblocking]];

    /// Copies a value from the first occupied slot without advancing the read position.
    /// @note This method is only safe to call from the consumer.
    /// @param value A reference to receive the value.
    /// @return false if the queue is empty.
    [[nodiscard]] bool peek(T &value) const noexcept [[clang::nonblocking]];

    // MARK: Discarding Values

    /// Discards values and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @param count The maximum number of values to discard.
    /// @return The number of values actually discarded.
    SizeType discard(SizeType count = 1) noexcept [[clang::nonblocking]];

    /// Discards all values and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @return The number of values discarded.
    SizeType drain() noexcept [[clang::nonblocking]];

    // MARK: Advanced Writing and Reading

    /// Returns a write vector containing the current writable space.
    /// @note This method is only safe to call from the producer.
    /// @return A pair of spans containing the current writable space.
    [[nodiscard]] WriteVector writeVector() noexcept [[clang::nonblocking]];

    /// Finalizes a write transaction by writing staged data to the ring buffer.
    /// @warning The behavior is undefined if count is greater than the free space in the write vector.
    /// @note This method is only safe to call from the producer.
    /// @param count The number of values that were successfully written to the write vector.
    void commitWrite(SizeType count) noexcept [[clang::nonblocking]];

    /// Returns a read vector containing the current readable data.
    /// @note This method is only safe to call from the consumer.
    /// @return A pair of spans containing the current readable data.
    [[nodiscard]] ReadVector readVector() const noexcept [[clang::nonblocking]];

    /// Finalizes a read transaction by removing data from the front of the ring buffer.
    /// @warning The behavior is undefined if count is greater than the available data in the read vector.
    /// @note This method is only safe to call from the consumer.
    /// @param count The number of values that were successfully read from the read vector.
    void commitRead(SizeType count) noexcept [[clang::nonblocking]];

  private:
    /// The buffer containing the values.
    T buffer_[N];

    /// The size of buffer_  minus one.
    static constexpr auto capacityMask_ = N - 1;

    /// The free-running write location.
    alignas(std::hardware_destructive_interference_size) AtomicSizeType writePosition_{0};
    /// The free-running read location.
    alignas(std::hardware_destructive_interference_size) AtomicSizeType readPosition_{0};

    static_assert(AtomicSizeType::is_always_lock_free, "Lock-free AtomicSizeType required");
};

// MARK: - Implementation -

// MARK: Information

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline auto Queue<T, N>::writePosition() const noexcept -> SizeType {
    return writePosition_.load(std::memory_order_relaxed);
}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline auto Queue<T, N>::readPosition() const noexcept -> SizeType {
    return readPosition_.load(std::memory_order_relaxed);
}

// MARK: Statistics

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline bool Queue<T, N>::isFull() const noexcept {
    const auto writePos = writePosition_.load(std::memory_order_relaxed);
    const auto readPos = readPosition_.load(std::memory_order_acquire);
    return (writePos - readPos) == N;
}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline bool Queue<T, N>::isEmpty() const noexcept {
    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    return writePos == readPos;
}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline auto Queue<T, N>::free() const noexcept -> SizeType {
    const auto writePos = writePosition_.load(std::memory_order_relaxed);
    const auto readPos = readPosition_.load(std::memory_order_acquire);
    return N - (writePos - readPos);
}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline auto Queue<T, N>::size() const noexcept -> SizeType {
    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    return writePos - readPos;
}

// MARK: Queue Operations

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline bool Queue<T, N>::push(const T &value) noexcept {
    const auto writePos = writePosition_.load(std::memory_order_relaxed);
    const auto readPos = readPosition_.load(std::memory_order_acquire);
    const auto used = writePos - readPos;

    if (used == N) {
        return false;
    }

    buffer_[writePos & capacityMask_] = value;
    writePosition_.store(writePos + 1, std::memory_order_release);
    return true;
}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline bool Queue<T, N>::pop(T &value) noexcept {
    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);

    if (writePos == readPos) {
        return false;
    }

    value = buffer_[readPos & capacityMask_];
    readPosition_.store(readPos + 1, std::memory_order_release);
    return true;
}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline bool Queue<T, N>::peek(T &value) const noexcept {
    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);

    if (writePos == readPos) {
        return false;
    }

    value = buffer_[readPos & capacityMask_];
    return true;
}

// MARK: Discarding Values

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline auto Queue<T, N>::discard(SizeType count) noexcept -> SizeType {
    if (count == 0) [[unlikely]] {
        return 0;
    }

    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    const auto used = writePos - readPos;

    if (used == 0) {
        return 0;
    }

    const auto n = std::min(used, count);
    readPosition_.store(readPos + n, std::memory_order_release);
    return n;
}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline auto Queue<T, N>::drain() noexcept -> SizeType {
    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    const auto used = writePos - readPos;

    if (used == 0) {
        return 0;
    }

    readPosition_.store(writePos, std::memory_order_release);
    return used;
}

// MARK: Advanced Writing and Reading

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline auto Queue<T, N>::writeVector() noexcept -> WriteVector {
    const auto writePos = writePosition_.load(std::memory_order_relaxed);
    const auto readPos = readPosition_.load(std::memory_order_acquire);
    const auto used = writePos - readPos;
    const auto free = N - used;

    if (free == 0) [[unlikely]] {
        return {};
    }

    const auto writeIndex = writePos & capacityMask_;
    const auto toEnd = N - writeIndex;

    if (free > toEnd) [[unlikely]] {
        return {{buffer_ + writeIndex, toEnd}, {buffer_, free - toEnd}};
    }
    return {{buffer_ + writeIndex, free}, {}};
}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline void Queue<T, N>::commitWrite(SizeType count) noexcept {
    const auto writePos = writePosition_.load(std::memory_order_relaxed);
    writePosition_.store(writePos + count, std::memory_order_release);
}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline auto Queue<T, N>::readVector() const noexcept -> ReadVector {
    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    const auto used = writePos - readPos;

    if (used == 0) [[unlikely]] {
        return {};
    }

    const auto readIndex = readPos & capacityMask_;
    const auto toEnd = N - readIndex;

    if (used > toEnd) [[unlikely]] {
        return {{buffer_ + readIndex, toEnd}, {buffer_, used - toEnd}};
    }
    return {{buffer_ + readIndex, used}, {}};
}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline void Queue<T, N>::commitRead(SizeType count) noexcept {
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    readPosition_.store(readPos + count, std::memory_order_release);
}

} /* namespace spsc */

#endif
