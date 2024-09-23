
#include "dro/mpmc-queue.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

int main(int argc, char* argv[])
{

  {
    dro::MPMC_Queue<int> q {1};
    int t = 0;
    assert(q.try_push(1) == true);
    assert(q.size() == 1 && ! q.empty());
    assert(q.try_push(2) == false);
    assert(q.size() == 1 && ! q.empty());
    assert(q.try_pop(t) == true && t == 1);
    assert(q.size() == 0 && q.empty());
    assert(q.try_pop(t) == false && t == 1);
    assert(q.size() == 0 && q.empty());
  }

  // Copyable only type
  {
    struct Test
    {
      Test() {}
      Test(const Test&) noexcept {}
      Test& operator=(const Test&) noexcept { return *this; }
      Test(Test&&) = delete;
    };
    dro::MPMC_Queue<Test> q {16};
    // lvalue
    Test v;
    q.emplace(v);
    q.try_emplace(v);
    q.push(v);
    q.try_push(v);
    // xvalue
    q.push(Test());
    q.try_push(Test());
  }

  // Movable only type
  {
    dro::MPMC_Queue<std::unique_ptr<int>> q {16};
    // lvalue
    auto v = std::make_unique<int>(1);
    // q.emplace(v);
    // q.try_emplace(v);
    // q.push(v);
    // q.try_push(v);
    // xvalue
    q.emplace(std::make_unique<int>(1));
    q.try_emplace(std::make_unique<int>(1));
    q.push(std::make_unique<int>(1));
    q.try_push(std::make_unique<int>(1));
  }

  {
    bool throws = false;
    try
    {
      dro::MPMC_Queue<int> q {0};
    }
    catch (std::exception&)
    {
      throws = true;
    }
    assert(throws == true);
  }

  // Fuzz test
  {
    const uint64_t numOps     = 1000;
    const uint64_t numThreads = 10;
    dro::MPMC_Queue<uint64_t> q {numThreads};
    std::atomic<bool> flag(false);
    std::vector<std::thread> threads;
    std::atomic<uint64_t> sum(0);
    for (uint64_t i = 0; i < numThreads; ++i)
    {
      threads.push_back(std::thread([&, i] {
        while (! flag);
        for (auto j = i; j < numOps; j += numThreads) { q.push(j); }
      }));
    }
    for (uint64_t i = 0; i < numThreads; ++i)
    {
      threads.push_back(std::thread([&, i] {
        while (! flag);
        uint64_t threadSum = 0;
        for (auto j = i; j < numOps; j += numThreads)
        {
          uint64_t v;
          q.pop(v);
          threadSum += v;
        }
        sum += threadSum;
      }));
    }
    flag = true;
    for (auto& thread : threads) { thread.join(); }
    assert(sum == numOps * (numOps - 1) / 2);
  }

  return 0;
}
