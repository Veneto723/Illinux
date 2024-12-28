all: mkfs

mkfs: mkfs.c
	$(CC) $(CFLAGS) -o $@ $^

run: mkfs
	$(MAKE) -C ../user clean
	$(MAKE) -C ../user
	./mkfs kfs.raw ../user/bin/trek ../user/bin/rule30 ../user/bin/init0 ../user/bin/init1 ../user/bin/init2 ../user/bin/init3 ../user/bin/init4 ../user/bin/init5 ../user/bin/init6 ../user/bin/init7 ../user/bin/init8 ../user/bin/test.txt ../user/bin/test_lock.txt

clean:
	rm -rf *.o *.elf *.asm mkfs
