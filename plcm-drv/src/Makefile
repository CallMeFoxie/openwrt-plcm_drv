ifneq ($(KERNELRELEASE),)

obj-m += plcm_drv.o

else

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(CURDIR)
CC ?= gcc

.PHONY: all module tools clean

all: module

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

tools:
	$(CC) $(CPPFLAGS) $(CFLAGS) -O2 -Wall -Wextra plcm_test.c -o plcm_test
	$(CC) $(CPPFLAGS) $(CFLAGS) -O2 -Wall -Wextra plcm_cursor_char.c -o plcm_cursor_char
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=gnu11 -O2 -Wall -Wextra plcm_status.c -o plcm-status

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(RM) plcm_test plcm_cursor_char plcm-status

endif

