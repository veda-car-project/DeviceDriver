#	CrossCompile용 
# obj-m := tankmotor_drv.o
# KDIR := /usr/src/linux
# PWD  := $(shell pwd)

# default:
# 	$(MAKE) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C $(KDIR) M=$(PWD) modules

# clean:
# 	$(MAKE) -C $(KDIR) M=$(PWD) clean

# prepare:
# 	$(MAKE) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C $(KDIR) modules_prepare

#	Rpi Build용 
obj-m := tankmotor_drv.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean