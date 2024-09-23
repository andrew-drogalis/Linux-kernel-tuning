#include "dro/mpmc-queue.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <numeric>
#include <thread>

#if __has_include(<rigtorp/MPMCQueue.h> )
#include <rigtorp/MPMCQueue.h>
#endif

#if __has_include(<boost/lockfree/queue.hpp> )
#include <boost/lockfree/queue.hpp>
#endif

#if __has_include(<concurrentqueue/moodycamel/concurrentqueue.h>)
#include <concurrentqueue/moodycamel/concurrentqueue.h>
#endif

void pinThread(int cpu)
{
  if (cpu < 0)
  {
    return;
  }
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);
  CPU_SET(cpu, &cpu_set);
  if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpu_set) == 1)
  {
    perror("pthread_setaffinity_np");
    exit(1);
  }
}

int main(int argc, char* argv[])
{
  int cpu1 {-1};
  int cpu2 {-1};
  int cpu3 {-1};
  int cpu4 {-1};

  if (argc == 5)
  {
    cpu1 = std::stoi(argv[1]);
    cpu2 = std::stoi(argv[2]);
    cpu3 = std::stoi(argv[3]);
    cpu4 = std::stoi(argv[4]);
  }

  // Alignas powers of 2 for convenient testing of various sizes
  struct alignas(4) TestSize
  {
    int x_;
    TestSize() = default;
    TestSize(int x) : x_(x) {}
  };

  const std::size_t trialSize {7};
  static_assert(trialSize % 2, "Trial size must be odd");

  const std::size_t queueSize {10'000'000};
  const std::size_t iters {10'000'000};
  std::vector<std::size_t> operations1P1C(trialSize);
  std::vector<std::size_t> operations2P2C(trialSize);
  std::vector<std::size_t> roundTripTime(trialSize);

  std::cout << "dro::MPMC_Queue: \n";
  for (int i {}; i < trialSize; ++i)
  {
    {
      dro::MPMC_Queue<TestSize> queue(queueSize);
      auto thrd = std::thread([&]() {
        pinThread(cpu1);
        for (int i {}; i < iters; ++i)
        {
          TestSize val;
          while (! queue.try_pop(val)) {}
        }
      });

      pinThread(cpu2);

      auto start = std::chrono::steady_clock::now();
      for (int i {}; i < iters; ++i) { queue.emplace(TestSize(i)); }
      thrd.join();
      auto stop = std::chrono::steady_clock::now();

      operations1P1C[i] =
          iters * 1'000'000 /
          std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start)
              .count();
    }

    {
      dro::MPMC_Queue<TestSize> queue(queueSize);
      auto thrd = std::thread([&]() {
        pinThread(cpu1);
        for (int i {}; i < iters; ++i)
        {
          TestSize val;
          while (! queue.try_pop(val)) {}
        }
      });

      auto thrd2 = std::thread([&]() {
        pinThread(cpu3);
        for (int i {}; i < iters; ++i)
        {
          TestSize val;
          while (! queue.try_pop(val)) {}
        }
      });

      auto thrd3 = std::thread([&]() {
        pinThread(cpu4);
        for (int i {}; i < iters; ++i) { queue.emplace(TestSize(i)); }
      });

      pinThread(cpu2);

      auto start = std::chrono::steady_clock::now();
      for (int i {}; i < iters; ++i) { queue.emplace(TestSize(i)); }
      thrd.join();
      thrd2.join();
      thrd3.join();
      auto stop = std::chrono::steady_clock::now();

      operations2P2C[i] =
          iters * 1'000'000 /
          std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start)
              .count();
    }

    {
      dro::MPMC_Queue<TestSize> q1(queueSize), q2(queueSize);
      auto thrd = std::thread([&]() {
        pinThread(cpu1);
        for (int i {}; i < iters; ++i)
        {
          TestSize val;
          while (! q1.try_pop(val)) {}
          q2.emplace(val);
        }
      });

      pinThread(cpu2);

      auto start = std::chrono::steady_clock::now();
      for (int i {}; i < iters; ++i)
      {
        q1.emplace(TestSize(i));
        TestSize val;
        while (! q2.try_pop(val)) {}
      }
      auto stop = std::chrono::steady_clock::now();
      thrd.join();
      roundTripTime[i] =
          std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start)
              .count() /
          iters;
    }
  }

  std::sort(operations1P1C.begin(), operations1P1C.end());
  std::sort(operations2P2C.begin(), operations2P2C.end());
  std::sort(roundTripTime.begin(), roundTripTime.end());
  std::cout << "Mean: "
            << std::accumulate(operations1P1C.begin(), operations1P1C.end(),
                               0) /
                   trialSize
            << " ops/ms - 1P 1C\n";
  std::cout << "Median: " << operations1P1C[trialSize / 2]
            << " ops/ms - 1P 1C\n";
  std::cout << "Mean: "
            << std::accumulate(operations2P2C.begin(), operations2P2C.end(),
                               0) /
                   trialSize
            << " ops/ms - 2P 2C\n";
  std::cout << "Median: " << operations2P2C[trialSize / 2]
            << " ops/ms - 2P 2C\n";
  std::cout << "Mean: "
            << std::accumulate(roundTripTime.begin(), roundTripTime.end(), 0) /
                   trialSize
            << " ns RTT \n";
  std::cout << "Median: " << roundTripTime[trialSize / 2] << " ns RTT \n";

#if __has_include(<rigtorp/MPMCQueue.h> )

  std::cout << "rigtorp::MPMCQueue: \n";
  for (int i {}; i < trialSize; ++i)
  {
    {
      rigtorp::MPMCQueue<TestSize> queue(queueSize);
      auto thrd = std::thread([&]() {
        pinThread(cpu1);
        for (int i {}; i < iters; ++i)
        {
          TestSize val;
          while (! queue.try_pop(val)) {}
        }
      });

      pinThread(cpu2);

      auto start = std::chrono::steady_clock::now();
      for (int i {}; i < iters; ++i) { queue.emplace(TestSize(i)); }
      thrd.join();
      auto stop = std::chrono::steady_clock::now();

      operations1P1C[i] =
          iters * 1000000 /
          std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start)
              .count();
    }

    {
      rigtorp::MPMCQueue<TestSize> queue(queueSize);
      auto thrd = std::thread([&]() {
        pinThread(cpu1);
        for (int i {}; i < iters; ++i)
        {
          TestSize val;
          while (! queue.try_pop(val)) {}
        }
      });

      auto thrd2 = std::thread([&]() {
        pinThread(cpu3);
        for (int i {}; i < iters; ++i)
        {
          TestSize val;
          while (! queue.try_pop(val)) {}
        }
      });

      auto thrd3 = std::thread([&]() {
        pinThread(cpu4);
        for (int i {}; i < iters; ++i) { queue.emplace(TestSize(i)); }
      });

      pinThread(cpu2);

      auto start = std::chrono::steady_clock::now();
      for (int i {}; i < iters; ++i) { queue.emplace(TestSize(i)); }
      thrd.join();
      thrd2.join();
      thrd3.join();
      auto stop = std::chrono::steady_clock::now();

      operations2P2C[i] =
          iters * 1000000 /
          std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start)
              .count();
    }

    {
      rigtorp::MPMCQueue<TestSize> q1(queueSize), q2(queueSize);
      auto thrd = std::thread([&]() {
        pinThread(cpu1);
        for (int i {}; i < iters; ++i)
        {
          TestSize val;
          while (! q1.try_pop(val)) {}
          q2.emplace(val);
        }
      });

      pinThread(cpu2);

      auto start = std::chrono::steady_clock::now();
      for (int i {}; i < iters; ++i)
      {
        q1.emplace(TestSize(i));
        TestSize val;
        while (! q2.try_pop(val)) {}
      }
      auto stop = std::chrono::steady_clock::now();
      thrd.join();
      roundTripTime[i] =
          std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start)
              .count() /
          iters;
    }
  }

  std::sort(operations1P1C.begin(), operations1P1C.end());
  std::sort(operations2P2C.begin(), operations2P2C.end());
  std::sort(roundTripTime.begin(), roundTripTime.end());
  std::cout << "Mean: "
            << std::accumulate(operations1P1C.begin(), operations1P1C.end(),
                               0) /
                   trialSize
            << " ops/ms - 1P 1C\n";
  std::cout << "Median: " << operations1P1C[trialSize / 2]
            << " ops/ms - 1P 1C\n";
  std::cout << "Mean: "
            << std::accumulate(operations2P2C.begin(), operations2P2C.end(),
                               0) /
                   trialSize
            << " ops/ms - 2P 2C\n";
  std::cout << "Median: " << operations2P2C[trialSize / 2]
            << " ops/ms - 2P 2C\n";
  std::cout << "Mean: "
            << std::accumulate(roundTripTime.begin(), roundTripTime.end(), 0) /
                   trialSize
            << " ns RTT \n";
  std::cout << "Median: " << roundTripTime[trialSize / 2] << " ns RTT \n";

#endif

#if __has_include(<boost/lockfree/queue.hpp> )
  std::cout << "boost::lockfree::queue:" << std::endl;
  for (int i {}; i < trialSize; ++i)
  {
    {
      boost::lockfree::queue<TestSize> q(queueSize);
      auto t = std::thread([&] {
        pinThread(cpu1);
        for (int i = 0; i < iters; ++i)
        {
          TestSize val;
          while (! q.pop(val));
        }
      });

      pinThread(cpu2);

      auto start = std::chrono::steady_clock::now();
      for (int i = 0; i < iters; ++i) { while (! q.push(TestSize(i))); }
      t.join();
      auto stop = std::chrono::steady_clock::now();
      operations1P1C[i] =
          iters * 1000000 /
          std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start)
              .count();
    }

    {
      boost::lockfree::queue<TestSize> q(queueSize);
      auto t = std::thread([&] {
        pinThread(cpu1);
        for (int i = 0; i < iters; ++i)
        {
          TestSize val;
          while (! q.pop(val)) {}
        }
      });

      auto t2 = std::thread([&]() {
        pinThread(cpu3);
        for (int i {}; i < iters; ++i)
        {
          TestSize val;
          while (! q.pop(val)) {}
        }
      });

      auto t3 = std::thread([&]() {
        pinThread(cpu4);
        for (int i {}; i < iters; ++i)
        {
          while (! q.push(TestSize(i))) {}
        }
      });

      pinThread(cpu2);

      auto start = std::chrono::steady_clock::now();
      for (int i = 0; i < iters; ++i) { while (! q.push(TestSize(i))); }
      t.join();
      t2.join();
      t3.join();
      auto stop = std::chrono::steady_clock::now();
      operations2P2C[i] =
          iters * 1000000 /
          std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start)
              .count();
    }

    {
      boost::lockfree::queue<TestSize> q1(queueSize), q2(queueSize);
      auto t = std::thread([&] {
        pinThread(cpu1);
        for (int i = 0; i < iters; ++i)
        {
          TestSize val;
          while (! q1.pop(val));
          while (! q2.push(val));
        }
      });

      pinThread(cpu2);

      auto start = std::chrono::steady_clock::now();
      for (int i = 0; i < iters; ++i)
      {
        while (! q1.push(TestSize(i)));
        TestSize val;
        while (! q2.pop(val));
      }
      auto stop = std::chrono::steady_clock::now();
      t.join();
      roundTripTime[i] =
          std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start)
              .count() /
          iters;
    }
  }

  std::sort(operations1P1C.begin(), operations1P1C.end());
  std::sort(operations2P2C.begin(), operations2P2C.end());
  std::sort(roundTripTime.begin(), roundTripTime.end());
  std::cout << "Mean: "
            << std::accumulate(operations1P1C.begin(), operations1P1C.end(),
                               0) /
                   trialSize
            << " ops/ms - 1P 1C\n";
  std::cout << "Median: " << operations1P1C[trialSize / 2]
            << " ops/ms - 1P 1C\n";
  std::cout << "Mean: "
            << std::accumulate(operations2P2C.begin(), operations2P2C.end(),
                               0) /
                   trialSize
            << " ops/ms - 2P 2C\n";
  std::cout << "Median: " << operations2P2C[trialSize / 2]
            << " ops/ms - 2P 2C\n";
  std::cout << "Mean: "
            << std::accumulate(roundTripTime.begin(), roundTripTime.end(), 0) /
                   trialSize
            << " ns RTT \n";
  std::cout << "Median: " << roundTripTime[trialSize / 2] << " ns RTT \n";

#endif

#if __has_include(<concurrentqueue/moodycamel/concurrentqueue.h>)
  std::cout << "moodycamel::ConcurrentQueue" << std::endl;
  for (int i {}; i < trialSize; ++i)
  {
    {
      moodycamel::ConcurrentQueue<TestSize> q(queueSize);
      auto t = std::thread([&] {
        pinThread(cpu1);
        for (int i = 0; i < iters; ++i)
        {
          TestSize val {};
          while (! q.try_dequeue(val)) {}
        }
      });

      pinThread(cpu2);

      auto start = std::chrono::steady_clock::now();
      for (int i = 0; i < iters; ++i) { q.enqueue(TestSize(i)); }
      t.join();
      auto stop = std::chrono::steady_clock::now();
      operations1P1C[i] =
          iters * 1000000 /
          std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start)
              .count();
    }

    {
      moodycamel::ConcurrentQueue<TestSize> q(queueSize);
      auto t = std::thread([&] {
        pinThread(cpu1);
        for (int i = 0; i < iters; ++i)
        {
          TestSize val {};
          while (! q.try_dequeue(val)) {}
        }
      });

      auto t2 = std::thread([&]() {
        pinThread(cpu3);
        for (int i {}; i < iters; ++i)
        {
          TestSize val;
          while (! q.try_dequeue(val)) {}
        }
      });

      auto t3 = std::thread([&]() {
        pinThread(cpu4);
        for (int i {}; i < iters; ++i)
        {
          while (! q.enqueue(TestSize(i))) {}
        }
      });

      pinThread(cpu2);

      auto start = std::chrono::steady_clock::now();
      for (int i = 0; i < iters; ++i) { q.enqueue(TestSize(i)); }
      t.join();
      t2.join();
      t3.join();
      auto stop = std::chrono::steady_clock::now();
      operations2P2C[i] =
          iters * 1000000 /
          std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start)
              .count();
    }

    {
      moodycamel::ConcurrentQueue<TestSize> q1(queueSize), q2(queueSize);
      auto t = std::thread([&] {
        pinThread(cpu1);
        for (int i = 0; i < iters; ++i)
        {
          TestSize val {};
          while (! q1.try_dequeue(val)) {}
          q2.enqueue(val);
        }
      });

      pinThread(cpu2);

      auto start = std::chrono::steady_clock::now();
      for (int i = 0; i < iters; ++i)
      {
        q1.enqueue(TestSize(i));
        TestSize val {};
        while (! q2.try_dequeue(val)) {}
      }
      auto stop = std::chrono::steady_clock::now();
      t.join();
      roundTripTime[i] =
          std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start)
              .count() /
          iters;
    }
  }

  std::sort(operations1P1C.begin(), operations1P1C.end());
  std::sort(operations2P2C.begin(), operations2P2C.end());
  std::sort(roundTripTime.begin(), roundTripTime.end());
  std::cout << "Mean: "
            << std::accumulate(operations1P1C.begin(), operations1P1C.end(),
                               0) /
                   trialSize
            << " ops/ms - 1P 1C\n";
  std::cout << "Median: " << operations1P1C[trialSize / 2]
            << " ops/ms - 1P 1C\n";
  std::cout << "Mean: "
            << std::accumulate(operations2P2C.begin(), operations2P2C.end(),
                               0) /
                   trialSize
            << " ops/ms - 2P 2C\n";
  std::cout << "Median: " << operations2P2C[trialSize / 2]
            << " ops/ms - 2P 2C\n";
  std::cout << "Mean: "
            << std::accumulate(roundTripTime.begin(), roundTripTime.end(), 0) /
                   trialSize
            << " ns RTT \n";
  std::cout << "Median: " << roundTripTime[trialSize / 2] << " ns RTT \n";

#endif

  return 0;
}
