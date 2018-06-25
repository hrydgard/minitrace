CC=gcc
CXX=g++

INCLUDE=-I.
FLAGS=-Wall -W -O2 -DMTR_ENABLED

CFLAGS=$(INCLUDE) $(FLAGS)
CXXFLAGS=$(INCLUDE) $(FLAGS)
LDFLAGS=-lm

DEPS=minitrace.h
OBJS=minitrace.o minitrace_test.o
OBJS2=minitrace.o minitrace_test_mt.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

%.o: %.cpp $(DEPS)
	$(CXX) -c -o $@ $< $(CXXFLAGS)

all: minitrace_test minitrace_test_mt

minitrace_test: $(OBJS)
	$(CXX) -o $@ $^ ${CFLAGS}

minitrace_test_mt: $(OBJS2)
	$(CXX) -o $@ $^ -lpthread ${LDFLAGS}

clean:
	rm -f *.o *.d minitrace_test minitrace_test_mt
