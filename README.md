# MPMC Queue

## Benchmarks

These benchmarks were taken on a (4) core Intel(R) Core(TM) i5-9300H CPU @ 2.40GHz with isolcpus on cores 2 and 3. 
The linux kernel is v6.10.11-200.fc40.x86_64 and compiled with gcc version 14.2.1.

<img src="https://raw.githubusercontent.com/drogalis/MPMC-Queue/refs/heads/main/assets/Operations%20per%20Millisecond%20(1P%201C).png" alt="Operations Per Millisecond Stats" style="padding-top: 10px;">

<img src="https://raw.githubusercontent.com/drogalis/MPMC-Queue/refs/heads/main/assets/Operations%20per%20Millisecond%20(2P%202C).png" alt="Operations Per Millisecond Stats" style="padding-top: 10px;">

<img src="https://raw.githubusercontent.com/drogalis/MPMC-Queue/refs/heads/main/assets/Round%20Trip%20Time%20(ns).png" alt="Round Trip Time Stats" style="padding-top: 10px;">

#### Reduced Cache Misses

Without atomic turn alignment:

<img src="https://raw.githubusercontent.com/drogalis/MPMC-Queue/refs/heads/main/assets/perf-no-turn-alignment.png" alt="Perf without turn alignment" style="padding: 5px 0;">

With atomic turn alignment:

<img src="https://raw.githubusercontent.com/drogalis/MPMC-Queue/refs/heads/main/assets/perf-yes-turn-alignment.png" alt="Perf with turn alignment" style="padding: 5px 0;">


Full write up TBD.

## Installing

To build and install the shared library, run the commands below.

```
    $ mkdir build && cd build
    $ cmake .. -DCMAKE_BUILD_TYPE=Release
    $ sudo make install
```

## Sources

- [Rigtorp MPMCQueue]()
- [MoodyCamel ]()
- [Folly ]()
- [Boost]()



