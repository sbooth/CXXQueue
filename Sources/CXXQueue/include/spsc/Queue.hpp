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
#include <utility>

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
/// consumer operations. Calling producer APIs concurrently from multiple threads or consumer APIs concurrently from
/// multiple threads results in undefined behavior.
template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
class Queue final {
  public:
    /// Unsigned integer type.
    using SizeType = std::size_t;
    /// Atomic unsigned integer type.
    using AtomicSizeType = std::atomic<SizeType>;

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

    /// Returns the number of unoccupied positions in the queue.
    /// @note The result of this method is only accurate when called from the producer.
    /// @return The number of unoccupied positions available for writing.
    [[nodiscard]] SizeType availableToWrite() const noexcept [[clang::nonblocking]];

    /// Returns the number of occupied positions in the queue.
    /// @note The result of this method is only accurate when called from the consumer.
    /// @return The number of occupied positions available for reading.
    [[nodiscard]] SizeType availableToRead() const noexcept [[clang::nonblocking]];

    // MARK: Queue Operations

    /// Copies a value to the back of the queue and advances the write position.
    /// @note This method is only safe to call from the producer.
    /// @warning This method invalidates all open write transactions.
    /// @param value The value to copy.
    /// @return false if the queue is full.
    [[nodiscard]] bool push(const T &value) noexcept [[clang::nonblocking]];

    /// Copies a value from the front of the queue and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @warning This method invalidates all open read transactions.
    /// @param value A reference to receive the value.
    /// @return false if the queue is empty.
    [[nodiscard]] bool pop(T &value) noexcept [[clang::nonblocking]];

    /// Copies a value from the front of the queue without advancing the read position.
    /// @note This method is only safe to call from the consumer.
    /// @param value A reference to receive the value.
    /// @return false if the queue is empty.
    [[nodiscard]] bool peek(T &value) const noexcept [[clang::nonblocking]];

    // MARK: Discarding Values

    /// Discards values from the front of the queue and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @param count The maximum number of values to discard.
    /// @return The number of values actually discarded.
    SizeType discard(SizeType count = 1) noexcept [[clang::nonblocking]];

    /// Discards all values from the queue and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @return The number of values discarded.
    SizeType discardAll() noexcept [[clang::nonblocking]];

    // MARK: Advanced Writing and Reading

    /// A write transaction.
    class WriteTransaction final {
      public:
        /// The first writable span.
        std::span<T> first;
        /// The second writable span.
        std::span<T> second;

        /// Returns the number of positions available to write.
        /// @return The number of positions available for writing.
        [[nodiscard]] SizeType availableToWrite() const noexcept;

        /// Finalizes the write transaction by committing staged data to the back of the queue.
        /// @param count The number of values that were written.
        /// @return false if count exceeds the writable space or the transaction has already been committed.
        [[nodiscard]] bool commit(SizeType count) noexcept;

        WriteTransaction(const WriteTransaction &) = delete;
        WriteTransaction &operator=(const WriteTransaction &) = delete;

        WriteTransaction(WriteTransaction &&other) noexcept;
        WriteTransaction &operator=(WriteTransaction &&other) noexcept;

        /// Destroys the write transaction without committing.
        ~WriteTransaction() noexcept = default;

      private:
        /// Creates an empty write transaction.
        WriteTransaction() noexcept = default;

        /// Creates a write transaction.
        /// @param first The first writable span.
        /// @param second The second writable span.
        /// @param queue The owning queue.
        /// @param position The base write position.
        WriteTransaction(std::span<T> first, std::span<T> second, Queue *queue, SizeType position) noexcept;

        friend class Queue;
        /// The owning instance.
        Queue *queue_{nullptr};
        /// The write position at the time the transaction was created.
        SizeType position_{0};
    };

    /// Opens and returns a write transaction containing the current writable space.
    /// @note This method is only safe to call from the producer.
    /// @warning Opening multiple write transactions simultaneously is supported only if at most one of them is
    /// committed. After any producer operation all previously opened write transactions become invalid.
    /// @return A write transaction containing the current writable space.
    [[nodiscard]] WriteTransaction beginWrite() noexcept [[clang::nonblocking]];

    /// A read transaction.
    class ReadTransaction final {
      public:
        /// The first readable span.
        std::span<const T> first;
        /// The second readable span.
        std::span<const T> second;

        /// Returns the number of elements available to read.
        /// @return The number of elements available for reading.
        [[nodiscard]] SizeType availableToRead() const noexcept;

        /// Finalizes the read transaction by removing data from the front of the queue.
        /// @param count The number of values that were read.
        /// @return false if count exceeds the readable space or the transaction has already been committed.
        [[nodiscard]] bool commit(SizeType count) noexcept;

        ReadTransaction(const ReadTransaction &) = delete;
        ReadTransaction &operator=(const ReadTransaction &) = delete;

        ReadTransaction(ReadTransaction &&other) noexcept;
        ReadTransaction &operator=(ReadTransaction &&other) noexcept;

        /// Destroys the read transaction without committing.
        ~ReadTransaction() noexcept = default;

      private:
        /// Creates an empty read transaction.
        ReadTransaction() noexcept = default;

        /// Creates a read transaction.
        /// @param first The first readable span.
        /// @param second The second readable span.
        /// @param queue The owning queue.
        /// @param position The base read position.
        ReadTransaction(std::span<const T> first, std::span<const T> second, Queue *queue, SizeType position) noexcept;

        friend class Queue;
        /// The owning instance.
        Queue *queue_{nullptr};
        /// The read position at the time the transaction was created.
        SizeType position_{0};
    };

    /// Opens and returns a read transaction containing the current readable space.
    /// @note This method is only safe to call from the consumer.
    /// @warning Opening multiple read transactions simultaneously is supported only if at most one of them is
    /// committed. After any consumer operation all previously opened read transactions become invalid.
    /// @return A read transaction containing the current readable space.
    [[nodiscard]] ReadTransaction beginRead() noexcept [[clang::nonblocking]];

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
inline auto Queue<T, N>::availableToWrite() const noexcept -> SizeType {
    const auto writePos = writePosition_.load(std::memory_order_relaxed);
    const auto readPos = readPosition_.load(std::memory_order_acquire);
    return N - (writePos - readPos);
}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline auto Queue<T, N>::availableToRead() const noexcept -> SizeType {
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
inline auto Queue<T, N>::discardAll() noexcept -> SizeType {
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
inline auto Queue<T, N>::WriteTransaction::availableToWrite() const noexcept -> SizeType {
    return first.size() + second.size();
}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline bool Queue<T, N>::WriteTransaction::commit(SizeType count) noexcept {
    if (queue_ == nullptr || count > availableToWrite()) [[unlikely]] {
        return false;
    }

    queue_->writePosition_.store(position_ + count, std::memory_order_release);

    queue_ = nullptr;
    first = {};
    second = {};

    return true;
}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline Queue<T, N>::WriteTransaction::WriteTransaction(WriteTransaction &&other) noexcept
    : first(std::exchange(other.first, {})), second(std::exchange(other.second, {})),
      queue_(std::exchange(other.queue_, nullptr)), position_(std::exchange(other.position_, 0)) {}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline auto Queue<T, N>::WriteTransaction::operator=(WriteTransaction &&other) noexcept -> WriteTransaction & {
    if (this != &other) {
        first = std::exchange(other.first, {});
        second = std::exchange(other.second, {});
        queue_ = std::exchange(other.queue_, nullptr);
        position_ = std::exchange(other.position_, 0);
    }
    return *this;
}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline Queue<T, N>::WriteTransaction::WriteTransaction(std::span<T> first, std::span<T> second, Queue *queue,
                                                       SizeType position) noexcept
    : first(first), second(second), queue_(queue), position_(position) {}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline auto Queue<T, N>::beginWrite() noexcept -> WriteTransaction {
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
        return WriteTransaction({buffer_ + writeIndex, toEnd}, {buffer_, free - toEnd}, this, writePos);
    }
    return WriteTransaction({buffer_ + writeIndex, free}, {}, this, writePos);
}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline auto Queue<T, N>::ReadTransaction::availableToRead() const noexcept -> SizeType {
    return first.size() + second.size();
}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline bool Queue<T, N>::ReadTransaction::commit(SizeType count) noexcept {
    if (queue_ == nullptr || count > availableToRead()) [[unlikely]] {
        return false;
    }

    queue_->readPosition_.store(position_ + count, std::memory_order_release);

    queue_ = nullptr;
    first = {};
    second = {};

    return true;
}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline Queue<T, N>::ReadTransaction::ReadTransaction(ReadTransaction &&other) noexcept
    : first(std::exchange(other.first, {})), second(std::exchange(other.second, {})),
      queue_(std::exchange(other.queue_, nullptr)), position_(std::exchange(other.position_, 0)) {}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline auto Queue<T, N>::ReadTransaction::operator=(ReadTransaction &&other) noexcept -> ReadTransaction & {
    if (this != &other) {
        first = std::exchange(other.first, {});
        second = std::exchange(other.second, {});
        queue_ = std::exchange(other.queue_, nullptr);
        position_ = std::exchange(other.position_, 0);
    }
    return *this;
}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline Queue<T, N>::ReadTransaction::ReadTransaction(std::span<const T> first, std::span<const T> second, Queue *queue,
                                                     SizeType position) noexcept
    : first(first), second(second), queue_(queue), position_(position) {}

template <ValueLike T, std::size_t N>
    requires ValidPowerOfTwo<N>
inline auto Queue<T, N>::beginRead() noexcept -> ReadTransaction {
    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    const auto used = writePos - readPos;

    if (used == 0) [[unlikely]] {
        return {};
    }

    const auto readIndex = readPos & capacityMask_;
    const auto toEnd = N - readIndex;

    if (used > toEnd) [[unlikely]] {
        return ReadTransaction({buffer_ + readIndex, toEnd}, {buffer_, used - toEnd}, this, readPos);
    }
    return ReadTransaction({buffer_ + readIndex, used}, {}, this, readPos);
}

} /* namespace spsc */

#endif
