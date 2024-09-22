
#include <dro/mpmc-queue.hpp>
#include <thread>

int main(int argc, char *argv[]) {

  dro::MPMC_Queue<int> q {10};
  auto t1 = std::thread([&] {
    int v;
    q.pop(v);
  });
  auto t2 = std::thread([&] {
    int v;
    q.pop(v);
  });
  q.push(1);
  q.push(2);
  t1.join();
  t2.join();

  return 0;
}
