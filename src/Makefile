CFLAGS=-D_BSD_SOURCE -D_LARGEFILE64_SOURCE -Wall -ggdb -std=c99 -O0
CPPFLAGS=-Wall -ggdb

LIBS=-lbz2 -lexpat -lstdc++ -lxapian

main: bzpartial.o bzparse.o wkmain.o wkindex.o
	gcc $^ -o $@ $(LIBS)

clean:
	rm -f *.o a.out main
