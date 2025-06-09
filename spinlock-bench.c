#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

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
