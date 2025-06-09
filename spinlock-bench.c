#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ktime.h>
#include <linux/kthread.h>
#include <linux/atomic.h>


// critical/non-critical section design comes from glibc benchtests:
// https://github.com/bminor/glibc/blob/glibc-2.40/benchtests/bench-pthread-lock-base.c
#pragma GCC push_options
#pragma GCC optimize(1)

static int fibonacci(int i)
{
	asm("");
	if (i > 2)
		return fibonacci(i - 1) + fibonacci(i - 2);
	return 10 + i;
}

static void
do_filler(void)
{
	char buf1[512], buf2[512];
	int f = fibonacci(4);
	memcpy(buf1, buf2, f);
}

static void
do_filler_shared(void)
{
	static char buf1[512], buf2[512];
	int f = fibonacci(4);
	memcpy(buf1, buf2, f);
}

#pragma GCC pop_options

#define UNIT_WORK_CRT do_filler_shared()
#define UNIT_WORK_NON_CRT do_filler()

static inline void
critical_section(int length)
{
	for (int i = length; i >= 0; i--)
		UNIT_WORK_CRT;
}

static inline void
non_critical_section(int length)
{
	for (int i = length; i >= 0; i--)
		UNIT_WORK_NON_CRT;
}

typedef struct worker_params {
	long iters;
	int crit_len;
	int non_crit_len;
	atomic_t *alives;
	atomic_t *start;
	long duration_ns;
} worker_params_t;

DEFINE_SPINLOCK(my_spinlock);

static int worker(void *v)
{
	worker_params_t *p = (worker_params_t *)v;
	long iters = p->iters;

	// spin wait for starting
	atomic_inc(p->alives);
	while (!atomic_read(p->start))
		cpu_relax();

	ktime_t start_time = ktime_get();
	while (iters--)
	{
		unsigned long flags;

		spin_lock_irqsave(&my_spinlock, flags);
		critical_section(p->crit_len);
		spin_unlock_irqrestore(&my_spinlock, flags);

		non_critical_section(p->non_crit_len);
	}
	ktime_t end_time = ktime_get();
	p->duration_ns = ktime_to_ns(ktime_sub(end_time, start_time));

	atomic_dec(p->alives);
	return 0;
}

static long do_one_test(int nthreads, int crit_len, int non_crit_len, long iters)
{
	struct task_struct *tasks[nthreads];
	worker_params_t *p, params[nthreads];
	atomic_t alives;	// number of alives threads
	atomic_t start;		// should start bench

	atomic_set(&alives, 0);
	atomic_set(&start, 0);

	// launch threads to bench
	for (int i = 0; i < nthreads; i++)
	{
		p = &params[i];
		p->iters = iters;
		p->crit_len = crit_len;
		p->non_crit_len = non_crit_len;
		p->alives = &alives;
		p->start = &start;
		tasks[i] = kthread_create(worker, (void *)p, "spinlock-bench");
		wake_up_process(tasks[i]);
	}

	// wait for all thread started
	while (atomic_read(&alives) != nthreads)
		cpu_relax();

	atomic_set(&start, 1);  // start bench

	// wait for all thread exits
	while (atomic_read(&alives))
		cpu_relax();

	// teardown threads
	long sum_ns = 0;
	for (int i = 0; i < nthreads; i++)
	{
		sum_ns += params[i].duration_ns;
	}

	// return mean time for each thread
	return sum_ns/nthreads;
}

#define RUN_COUNT		10
#define MIN_TEST_NSEC	100000000  // 0.1s
#define START_ITERS		100000
#define MAX_ITERS		(LONG_MAX/1000000000)

static void bench(int nthreads, int crit_len, int non_crit_len)
{
	long iters = START_ITERS;
	long cur;
	long ts[RUN_COUNT + 2];


	// find a suitable iters for long enough tests
	while (true)
	{
		ktime_t start_time = ktime_get();
		cur = do_one_test(nthreads, crit_len, non_crit_len, iters);
		ktime_t end_time = ktime_get();
		long duration_ns = ktime_to_ns(ktime_sub(end_time, start_time));
		if (duration_ns > MIN_TEST_NSEC || iters > MAX_ITERS)
			break;
		iters *= 10;
	}

	ts[0] = cur;
	for (int i = 1; i < RUN_COUNT + 2; i++)
		ts[i] = do_one_test(nthreads, crit_len, non_crit_len, iters);

	// sort results to discard the fastest and slowest outliers
	for (int i = 0; i < RUN_COUNT + 1; i++)
		for (int j = i + 1; j < RUN_COUNT + 2; j++)
		{
			if (ts[i] > ts[j])
			{
				long tmp = ts[i];
				ts[i] = ts[j];
				ts[j] = tmp;
			}
		}

	long total_iters = iters * nthreads;
	for (int i = 1; i < RUN_COUNT + 1; i++)
	{
		printk("%ld\t", total_iters*1000000000/ts[i]);
	}
	printk("\n");
}

// Initialization function (called when the module is loaded)
static int __init spinlock_bench_init(void)
{
	printk(KERN_INFO "spinlock bench loaded\n");
	return 0; // Return 0 means success
}

// Exit function (called when the module is removed)
static void __exit spinlock_bench_exit(void)
{
	printk(KERN_INFO "spinlock bench unloaded\n");
}


// Register the functions
module_init(spinlock_bench_init);
module_exit(spinlock_bench_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simple spinlock bench Module");
MODULE_AUTHOR("Wangyang Guo");
