
all: cli test_mem flash

flash: flash.c common_utils.o swd.o rbpi.o
	cc -g $^ -o $@
test_mem: test_mem.c common_utils.o swd.o rbpi.o
	cc -g $^ -o $@

cli: cli.c common_utils.o swd.o rbpi.o linenoise/linenoise.c
	cc -g $^ -o $@

common_utils.o: common_utils.c
	cc -g -c $^ -o $@

swd.o: swd.c
	cc -g -c $^ -o $@
rbpi.o: rbpi.c
	cc -g -c $^ -o $@

clean:
	rm -rf *.o test_mem cli
