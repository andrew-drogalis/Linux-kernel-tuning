
// Andrew Drogalis Copyright (c) 2024, GNU 3.0 Licence
//
// Inspired from Erik Rigtorp
// Significant Modifications / Improvements
// E.G
// 1) Removed Raw Pointers
// 2) Utilized Vector for RAII memory management

#ifndef DRO_MPMC_QUEUE
#define DRO_MPMC_QUEUE

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>// for std::hardware_destructive_interference_size
#include <type_traits>
#include <utility>
#include <vector>

namespace dro
{
#ifdef __cpp_lib_hardware_interference_size
static constexpr std::size_t cacheLineSize =
    std::hardware_destructive_interference_size;
#else
static constexpr std::size_t cacheLineSize = 64;
#endif

template <typename T> struct alignas(cacheLineSize) Slot
{
  alignas(cacheLineSize) std::atomic<std::size_t> turn {0};
  T data_;

  template <typename... Args>
  explicit Slot(Args&&... args) : data_(K(std::forward<Args>(args)...))
  {
    static_assert(std::is_nothrow_constructible<T, Args&&...>::value,
                  "T must be nothrow constructible with Args");
  }

  ~Slot()
  {
    if (turn.load(std::memory_order_acquire) & 1)
    {
      destroy();
    }
  }

  void destroy() noexcept
  {
    static_assert(std::is_nothrow_destructible<T>::value,
                  "T must be nothrow destructible");
    data_.~T();
  }
  // These are required for vector static assert, but are never invoked
  Slot(const Slot& lhs)
      : data_(lhs.data_), turn(lhs.turn.load(std::memory_order_acquire))
  {
  }
  Slot& operator=(const Slot& lhs)
  {
    if (*this != lhs)
    {
      data_ = lhs.data_;
      turn  = lhs.turn.load(std::memory_order_acquire);
    }
    return *this;
  };
  Slot(Slot&& lhs) : data_(std::move(lhs.data_)) {}
  Slot& operator=(Slot&& lhs)
  {
    if (*this != lhs)
    {
      data_ = std::move(lhs.data_);
      // trivially copyable
      turn  = lhs.turn.load(std::memory_order_acquire);
    }
    return *this;
  };
  bool operator==(Slot& lhs)
  {
    return data_ == lhs.data_ && turn.load(std::memory_order_acquire) ==
                                     lhs.turn.load(std::memory_order_acquire);
  }
  bool operator!=(Slot& lhs) { return ! (data_ == lhs.data_); }
};

template <typename T, typename Allocator = std::allocator<Slot<T>>>
class MPMC_Queue
{
private:
  static_assert(std::is_nothrow_copy_assignable<T>::value ||
                    std::is_nothrow_move_assignable<T>::value,
                "T must be nothrow copy or move assignable");
  static_assert(std::is_nothrow_destructible<T>::value,
                "T must be nothrow destructible");

  std::size_t capacity_;
  std::vector<Slot<T>> buffer_;

  alignas(cacheLineSize) std::atomic<std::size_t> tail_ {0};
  alignas(cacheLineSize) std::atomic<std::size_t> head_ {0};

  [[nodiscard]] std::size_t turn(size_t index) const noexcept
  {
    return index / capacity_;
  }

public:
  explicit MPMC_Queue(const std::size_t capacity,
                      const Allocator& allocator = Allocator())
      : capacity_(capacity), buffer_(allocator)
  {
    if (capacity_ < 1)
    {
      capacity_ = 1;
    }
    ++capacity_;
    if (capacity_ > SIZE_MAX)
    {
      capacity_ = SIZE_MAX - 1;
    }
    buffer_.resize(capacity_);
  }

  ~MPMC_Queue() noexcept
  {
    for (size_t i {}; i < capacity_; ++i) { buffer_[i].destroy(); }
  }

  // non-copyable and non-movable
  MPMC_Queue(const MPMC_Queue& lhs)        = delete;
  MPMC_Queue(MPMC_Queue&& lhs)             = delete;
  MPMC_Queue& operator=(const MPMC_Queue&) = delete;
  MPMC_Queue& operator=(MPMC_Queue&&)      = delete;

  template <typename... Args> void emplace(Args&&... args) noexcept
  {
    static_assert(std::is_nothrow_constructible<T, Args&&...>::value,
                  "T must be nothrow constructible");
    auto const head = head_.fetch_add(1);
    auto& slot      = buffer_[head % capacity_];
    while (turn(head) * 2 != slot.turn.load(std::memory_order_acquire)) {}
    slot.data_ = T(std::forward<Args>(args)...);
    slot.turn.store(turn(head) * 2 + 1, std::memory_order_release);
  }

  template <typename... Args> bool try_emplace(Args&&... args) noexcept
  {
    static_assert(std::is_nothrow_constructible<T, Args&&...>::value,
                  "T must be nothrow constructible");
    auto head = head_.load(std::memory_order_acquire);
    while (true)
    {
      auto& slot = buffer_[head % capacity_];
      if (turn(head) * 2 == slot.turn.load(std::memory_order_acquire))
      {
        if (head_.compare_exchange_strong(head, head + 1))
        {
          slot.data_ = T(std::forward<Args>(args)...);
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

  void push(const T& val) noexcept
  {
    static_assert(std::is_nothrow_copy_constructible<T>::value,
                  "T must be nothrow copy constructible");
    emplace(val);
  }

  template <typename P, typename = typename std::enable_if<
                            std::is_nothrow_constructible<T, P&&>::value>::type>
  void push(P&& val) noexcept
  {
    emplace(std::forward<P>(val));
  }

  bool try_push(const T& val) noexcept
  {
    static_assert(std::is_nothrow_copy_constructible<T>::value,
                  "T must be nothrow copy constructible");
    return try_emplace(val);
  }

  template <typename P, typename = typename std::enable_if<
                            std::is_nothrow_constructible<T, P&&>::value>::type>
  bool try_push(P&& val) noexcept
  {
    return try_emplace(std::forward<P>(val));
  }

  bool pop(T& val) noexcept
  {
    auto const tail = tail_.fetch_add(1);
    auto& slot      = buffer_[tail % capacity_];
    while (turn(tail) * 2 + 1 != slot.turn.load(std::memory_order_acquire)) {}
    slot.destroy();
    slot.turn.store(turn(tail) * 2 + 2, std::memory_order_release);
  }

  bool try_pop(T& val) noexcept
  {
    auto tail = tail_.load(std::memory_order_acquire);
    while (true)
    {
      auto& slot = buffer_[tail % capacity_];
      if (turn(tail) * 2 + 1 == slot.turn.load(std::memory_order_acquire))
      {
        if (tail_.compare_exchange_strong(tail, tail + 1))
        {
          slot.destroy();
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

  [[nodiscard]] std::size_t size() const noexcept
  {
    std::ptrdiff_t diff = head_.load(std::memory_order_acquire) -
                          tail_.load(std::memory_order_acquire);
    if (diff < 0)
    {
      diff += capacity_;
    }
    return static_cast<std::size_t>(diff);
  }

  [[nodiscard]] bool empty() const noexcept
  {
    return head_.load(std::memory_order_acquire) ==
           tail_.load(std::memory_order_acquire);
  }
};
}// namespace dro
#endif
