## Linux Kernel spinlock Benchmark

Benchmark for testing Linux kernel spin locks performance with different threads and critical sections. The test scenario is similar to glibc [bench-pthread-lock](https://github.com/bminor/glibc/blob/glibc-2.40/benchtests/bench-pthread-lock-base.c).

The bench configuration consists of 3 parts:
1. thread number
2. critical-section length
3. non-critical-section length


### Usage

Compile and install the kernel module:
```bash
make
sudo make install
```

Run the test
```bash
echo "[nthreads] [crit_len] [non_crit_len] > /proc/spinlock_bench

# example: echo "16 2 8" > /proc/spinlock_bench
```

Check the results
```bash
cat /proc/spinlock_bench
```
The benchmark will run several rounds and show result in throughputs. The highest and lowest number are discarded as outliers.
