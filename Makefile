CFLAGS += -Wall

all: watchdogd

romfs:
	$(ROMFSINST) -e CONFIG_USER_WATCHDOGD /bin/$(EXEC)
	$(ROMFSINST) -e CONFIG_USER_WATCHDOGD -a "watchdogd:unknown:/bin/watchdogd -f -s" /etc/inittab

clean:
	-rm -f $(EXEC) *.elf *.gdb *.o

.PHONY: all clean romfs
