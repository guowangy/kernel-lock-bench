obj-m += spinlock-bench.o

all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install:
	sudo insmod spinlock-bench.ko

uninstall:
	sudo rmmod spinlock-bench
