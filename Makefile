TARGET_MOUDLE := gpio-boot-reset
obj-m += $(TARGET_MOUDLE).o
KERNEL_SRC := /lib/modules/$(shell uname -r )/build
SRC := $(shell pwd)

all:
	make -C $(KERNEL_SRC) M=$(SRC) modules
clean:
	rm gpio-boot-reset.ko gpio-boot-reset.mod.* gpio-boot-reset.o Module.symvers modules.order .cache.mk .gpio-boot-reset*
load:
	insmod ./$(TARGET_MOUDLE).ko
unload:
	rmmod ./$(TARGET_MOUDLE).ko
