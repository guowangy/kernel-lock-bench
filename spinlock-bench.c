#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

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

// Initialization function (called when the module is loaded)
static int __init spinlock_bench_init(void) {
	printk(KERN_INFO "spinlock bench loaded\n");
	return 0; // Return 0 means success
}

// Exit function (called when the module is removed)
static void __exit spinlock_bench_exit(void) {
	printk(KERN_INFO "spinlock bench unloaded\n");
}

// Register the functions
module_init(spinlock_bench_init);
module_exit(spinlock_bench_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simple spinlock bench Module");
MODULE_AUTHOR("Wangyang Guo");
