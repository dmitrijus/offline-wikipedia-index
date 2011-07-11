CFLAGS=-D_BSD_SOURCE -D_LARGEFILE64_SOURCE -Wall -ggdb -std=c99 -O0
LIBS=-lbz2 -lexpat

main: bzpartial.o bzparse.o
	gcc bzpartial.o bzparse.o -o $@ $(LIBS)

clean:
	rm -f *.o a.out main
