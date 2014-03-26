CC=gcc
CFLAGS=-I. -Wall -O2
DEPS=minitrace.h
OBJS=minitrace.o minitrace_test.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

minitrace_test: $(OBJS)
	gcc -o $@ $^ ${CFLAGS}

clean:
	rm *.o
	rm minitrace_test
