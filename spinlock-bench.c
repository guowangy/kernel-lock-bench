#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ktime.h>
#include <linux/kthread.h>
#include <linux/atomic.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/delay.h>


#define BUFSIZE 1024
static char proc_data[BUFSIZE];

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
	struct task_struct *task;
	worker_params_t *p;
	worker_params_t *params = kmalloc(sizeof(worker_params_t) * nthreads, GFP_KERNEL);
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
		task = kthread_create(worker, (void *)p, "spinlock-bench");
		wake_up_process(task);
	}

	// wait for all thread started
	while (atomic_read(&alives) != nthreads)
		cpu_relax();

	atomic_set(&start, 1);  // start bench

	// wait for all thread exits
	while (atomic_read(&alives))
		msleep(100);

	// teardown threads
	long sum_ns = 0;
	for (int i = 0; i < nthreads; i++)
	{
		sum_ns += params[i].duration_ns;
	}

	kfree(params);
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
	int remains = BUFSIZE;
	char *pos = proc_data;
	for (int i = 1; i < RUN_COUNT + 1; i++)
	{
		int written = snprintf(pos, remains, "%ld ",
				total_iters*1000000000/ts[i]);
		remains -= written;
		pos += written;
		if (remains <= 0)
		{
			printk("no enough buffer to write results\n");
			break;
		}
	}
	if (remains)
		snprintf(pos, remains, "\n");
}

DEFINE_MUTEX(bench_lock);

#define PROC_FILENAME	"spinlock_bench"

static int proc_show(struct seq_file *m, void *v)
{
	int ret = 0;
	mutex_lock(&bench_lock);
	int len = strnlen(proc_data, BUFSIZE);
	if (len >= BUFSIZE)
	{
		ret = -EFAULT;
		goto out;
	}
	seq_printf(m, "%s", proc_data);
out:
	mutex_unlock(&bench_lock);
	return 0;
}

static int proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_show, NULL);
}

static int parse_string_to_ints(const char *str, int *arr, int max_elements) {
	char *token;
	char *str_copy;
	int i = 0;

	// Make a copy of the input string
	str_copy = kstrdup(str, GFP_KERNEL);
	if (!str_copy)
		return -ENOMEM;

	// Split the string by spaces
	while ((token = strsep(&str_copy, " ")) != NULL && i < max_elements) {
		if (kstrtoint(token, 10, &arr[i]) == 0)
			i++;
		else
			printk(KERN_WARNING "Failed to convert '%s' to int\n", token);
	}

	kfree(str_copy);
	return i; // Return the number of parsed integers
}

#define NUM_ARGS	3

static ssize_t proc_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos) {
	int args[3];

	if (count > BUFSIZE)
		return -EFAULT;
	if (copy_from_user(proc_data, ubuf, count))
		return -EFAULT;
	proc_data[count] = '\0';

	int parsed = parse_string_to_ints(proc_data, args, NUM_ARGS);
	if (parsed != NUM_ARGS)
		return -EFAULT;

	// good, run the bench
	mutex_lock(&bench_lock);
	int nthreads = args[0];
	int crit_len = args[1];
	int non_crit_len = args[2];
	bench(nthreads, crit_len, non_crit_len);
	mutex_unlock(&bench_lock);
	return count;
}

static struct proc_ops proc_fops = {
	.proc_open = proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_write = proc_write,
};


// Initialization function (called when the module is loaded)
static int __init spinlock_bench_init(void)
{
	printk(KERN_INFO "spinlock bench loaded\n");
	proc_create(PROC_FILENAME, 0666, NULL, &proc_fops);
	return 0; // Return 0 means success
}

// Exit function (called when the module is removed)
static void __exit spinlock_bench_exit(void)
{
	remove_proc_entry(PROC_FILENAME, NULL);
	printk(KERN_INFO "spinlock bench unloaded\n");
}


// Register the functions
module_init(spinlock_bench_init);
module_exit(spinlock_bench_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simple spinlock bench Module");
MODULE_AUTHOR("Wangyang Guo");
