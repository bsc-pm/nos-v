
CC=gcc
CFLAGS=-O2 -g3
INCLUDE=-I/home/Computational/anavarro/installation/nos-v/include
LIBS=-lnosv -lpthread -lrt

all: test.x tasktype.x

test.x: test.c
	$(CC) $(CFLAGS) $(INCLUDE) -o test.x test.c $(LIBS)

tasktype.x: tasktype.c
	$(CC) $(CFLAGS) $(INCLUDE) -o tasktype.x tasktype.c $(LIBS)

clean:
	rm -f test.x tasktype.x
