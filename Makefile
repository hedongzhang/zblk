MOD = zblk
MOD_DIR = $(shell pwd)

KERNELDIR = /usr/src/kernels/`uname -r`

SOURCES = zblk_init.c
SOURCES += memblk.c linear.c

INCLUDES = -I$(MODDIR)
OBJECTS = $(SOURCES:%.c=%.o)

COMPILE_CFLAGS = -std=gnu99
EXTRA_CFLAGS = $(COMPILE_CFLAGS)

# KBUILD_EXTRA_SYMBOLS += other module symbols file	eg:/root/driver_demo/Module.symvers
# export KBUILD_EXTRA_SYMBOLS

obj-m := $(MOD).o
$(MOD)-y := $(OBJECTS)

all:
	make -C $(KERNELDIR) M=$(MOD_DIR)
clean:
	rm -f *.o *.mod.c *.order *.symvers *.ko
