#include "dro/mpmc-queue.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

int main(int argc, char* argv[])
{


// {
  //   const uint64_t numOps     = 1000;
  //   const uint64_t numThreads = 10;
  //   dro::MPMC_Queue<uint64_t> q {numThreads};
  //   std::atomic<bool> flag(false);
  //   std::vector<std::thread> threads;
  //   std::atomic<uint64_t> sum(0);
  //   for (uint64_t i = 0; i < numThreads; ++i)
  //   {
  //     threads.push_back(std::thread([&, i] {
  //       while (! flag);
  //       for (auto j = i; j < numOps; j += numThreads) { q.push(j); }
  //     }));
  //   }
  //   for (uint64_t i = 0; i < numThreads; ++i)
  //   {
  //     threads.push_back(std::thread([&, i] {
  //       while (! flag);
  //       uint64_t threadSum = 0;
  //       for (auto j = i; j < numOps; j += numThreads)
  //       {
  //         uint64_t v;
  //         q.pop(v);
  //         threadSum += v;
  //       }
  //       sum += threadSum;
  //     }));
  //   }
  //   flag = true;
  //   for (auto& thread : threads) { thread.join(); }
  //   assert(sum == numOps * (numOps - 1) / 2);
  // }
 return 0;
}

