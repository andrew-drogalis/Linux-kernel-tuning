// Andrew Drogalis Copyright (c) 2024, GNU 3.0 Licence
//
// Inspired from Erik Rigtorp
// Significant Modifications / Improvements
//
// 1) Removed Raw Pointers
// 2) Cannot use vector with Slot due to deleted copy and move constructor.
//    Manual allocation is the most straight forward solution.
// 3) Used copy assignment instead of placement new
// 4) Added C++20 concepts
// 5) Removed turn alignment
// 6) Added std::memory_order_relaxed for atomic fetch_add

#ifndef DRO_MPMC_QUEUE
#define DRO_MPMC_QUEUE

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>// for std::hardware_destructive_interference_size
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace dro
{

#ifdef __cpp_lib_hardware_interference_size
static constexpr std::size_t cacheLineSize =
    std::hardware_destructive_interference_size;
#else
static constexpr std::size_t cacheLineSize = 64;
#endif

template <typename T>
concept MPMC_Type =
    std::is_default_constructible<T>::value &&
    std::is_nothrow_destructible<T>::value &&
    (std::is_move_assignable_v<T> || std::is_copy_assignable_v<T>);

template <typename T, typename... Args>
concept MPMC_NoThrow_Type =
    std::is_nothrow_constructible_v<T, Args&&...> &&
    ((std::is_nothrow_copy_assignable_v<T> && std::is_copy_assignable_v<T>) ||
     (std::is_nothrow_move_assignable_v<T> && std::is_move_assignable_v<T>));

template <MPMC_Type T> struct alignas(cacheLineSize) Slot
{
  T data_ {};
  // This should not be aligned. Due to the aligned struct this doesn't cause
  // cache contention. Alignment here would increase memory footprint.
  std::atomic<std::size_t> turn {0};

  Slot() = default;
  ~Slot()
  {
    if (turn.load(std::memory_order_acquire) & 1)
    {
      destroy();
    }
  }
  Slot(const Slot&)            = delete;
  Slot& operator=(const Slot&) = delete;
  Slot(Slot&&)                 = delete;
  Slot& operator=(Slot&&)      = delete;

  void assign_value(T val) noexcept(MPMC_NoThrow_Type<T>)
    requires std::is_copy_assignable_v<T> && (! std::is_move_assignable_v<T>)
  {
    data_ = val;
  }

  void assign_value(T val) noexcept(MPMC_NoThrow_Type<T>)
    requires std::is_move_assignable_v<T>
  {
    data_ = std::move(val);
  }

  template <typename... Args>
    requires std::constructible_from<T, Args...> &&
             std::is_nothrow_constructible_v<T, Args...> &&
             std::is_copy_assignable_v<T> && (! std::is_move_assignable_v<T>)
  void assign_value(Args&&... args) noexcept(MPMC_NoThrow_Type<T, Args...>)
  {
    data_ = T(std::forward<Args>(args)...);
  }

  template <typename... Args>
    requires std::constructible_from<T, Args...> &&
             std::is_nothrow_constructible_v<T, Args...> &&
             std::is_move_assignable_v<T>
  void assign_value(Args&&... args) noexcept(MPMC_NoThrow_Type<T, Args...>)
  {
    data_ = std::move(T(std::forward<Args>(args)...));
  }

  T& return_value() noexcept(MPMC_NoThrow_Type<T>)
    requires std::is_copy_assignable_v<T> && (! std::is_move_assignable_v<T>)
  {
    return data_;
  }

  T&& return_value() noexcept(MPMC_NoThrow_Type<T>)
    requires std::is_move_assignable_v<T>
  {
    return std::move(data_);
  }

  void destroy() noexcept { data_.~T(); }
};

template <MPMC_Type T, typename Allocator = std::allocator<Slot<T>>>
class alignas(cacheLineSize) MPMC_Queue
{
private:
  std::size_t capacity_;
  Allocator allocator_ [[no_unique_address]];
  Slot<T>* buffer_;
  static constexpr std::size_t MAX_SIZE_T =
      std::numeric_limits<std::size_t>::max();

  alignas(cacheLineSize) std::atomic<std::size_t> tail_ {0};
  alignas(cacheLineSize) std::atomic<std::size_t> head_ {0};

  [[nodiscard]] std::size_t turn(size_t index) const noexcept
  {
    return index / capacity_;
  }

public:
  explicit MPMC_Queue(const std::size_t capacity,
                      const Allocator& allocator = Allocator())
      : capacity_(capacity), allocator_(allocator)
  {
    // Capacity cannot be negative
    if (capacity_ < 1)
    {
      throw std::logic_error("Capacity must be positive");
    }
    // - 1 is for the ++capacity_ argument (rare overflow edge case)
    if (capacity_ == MAX_SIZE_T)
    {
      capacity_ = MAX_SIZE_T - 1;
    }
    // ++capacity_;// prevents live lock e.g. reader and writer share 1 slot for
    // size 1
    buffer_ = allocator_.allocate(capacity_ + 1);
    for (size_t i {}; i < capacity_; ++i) { new (&buffer_[i]) Slot<T>(); }
  }

  ~MPMC_Queue() noexcept
  {
    for (size_t i {}; i < capacity_; ++i) { buffer_[i].~Slot(); }
    allocator_.deallocate(buffer_, capacity_ - 1);
  }

  // non-copyable and non-movable
  MPMC_Queue(const MPMC_Queue& lhs)        = delete;
  MPMC_Queue(MPMC_Queue&& lhs)             = delete;
  MPMC_Queue& operator=(const MPMC_Queue&) = delete;
  MPMC_Queue& operator=(MPMC_Queue&&)      = delete;

  template <typename... Args>
    requires std::constructible_from<T, Args...>
  void emplace(Args&&... args) noexcept(MPMC_NoThrow_Type<T, Args&&...>)
  {
    auto const head = head_.fetch_add(1, std::memory_order_relaxed);
    auto& slot      = buffer_[head % capacity_];
    while (turn(head) * 2 != slot.turn.load(std::memory_order_acquire)) {}
    slot.assign_value(std::forward<Args>(args)...);
    slot.turn.store(turn(head) * 2 + 1, std::memory_order_release);
  }

  template <typename... Args>
    requires std::constructible_from<T, Args...>
  [[nodiscard]] bool try_emplace(Args&&... args) noexcept(
      MPMC_NoThrow_Type<T, Args&&...>)
  {
    auto head = head_.load(std::memory_order_acquire);
    while (true)
    {
      auto& slot = buffer_[head % capacity_];
      if (turn(head) * 2 == slot.turn.load(std::memory_order_acquire))
      {
        if (head_.compare_exchange_strong(head, head + 1))
        {
          slot.assign_value(std::forward<Args>(args)...);
          slot.turn.store(turn(head) * 2 + 1, std::memory_order_release);
          return true;
        }
      }
      else
      {
        auto const prevHead = head;
        head                = head_.load(std::memory_order_acquire);
        if (head == prevHead)
        {
          return false;
        }
      }
    }
  }

  void push(const T& val) noexcept(MPMC_NoThrow_Type<T>) { emplace(val); }

  template <typename P>
    requires std::constructible_from<T, P&&>
  void push(P&& val) noexcept(MPMC_NoThrow_Type<T, P&&>)
  {
    emplace(std::forward<P>(val));
  }

  [[nodiscard]] bool try_push(const T& val) noexcept(MPMC_NoThrow_Type<T>)
  {
    return try_emplace(val);
  }

  template <typename P>
    requires std::constructible_from<T, P&&>
  [[nodiscard]] bool try_push(P&& val) noexcept(MPMC_NoThrow_Type<T, P&&>)
  {
    return try_emplace(std::forward<P>(val));
  }

  void pop(T& val) noexcept
  {
    auto const tail = tail_.fetch_add(1, std::memory_order_relaxed);
    auto& slot      = buffer_[tail % capacity_];
    while (turn(tail) * 2 + 1 != slot.turn.load(std::memory_order_acquire)) {}
    val = slot.return_value();
    slot.turn.store(turn(tail) * 2 + 2, std::memory_order_release);
  }

  [[nodiscard]] bool try_pop(T& val) noexcept
  {
    auto tail = tail_.load(std::memory_order_acquire);
    while (true)
    {
      auto& slot = buffer_[tail % capacity_];
      if (turn(tail) * 2 + 1 == slot.turn.load(std::memory_order_acquire))
      {
        if (tail_.compare_exchange_strong(tail, tail + 1))
        {
          val = slot.return_value();
          slot.turn.store(turn(tail) * 2 + 2, std::memory_order_release);
          return true;
        }
      }
      else
      {
        auto const prevTail = tail;
        tail                = tail_.load(std::memory_order_acquire);
        if (tail == prevTail)
        {
          return false;
        }
      }
    }
  }

  [[nodiscard]] std::ptrdiff_t size() const noexcept
  {
    // This is an approximate size
    auto tail = tail_.load(std::memory_order_acquire);
    auto head = head_.load(std::memory_order_acquire);
    auto maxToTail = MAX_SIZE_T - tail;
    // This needs to be a signed type, and causes a narrowing of size_t
    std::ptrdiff_t headToTail = head - tail;
    // Handles wrap around case
    return (maxToTail < std::abs(headToTail)) ? maxToTail + head : headToTail;
  }

  [[nodiscard]] bool empty() const noexcept
  {
    return size() <= 0;
  }

  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
};
}// namespace dro
#endif
