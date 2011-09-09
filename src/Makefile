CFLAGS=-D_BSD_SOURCE -D_LARGEFILE64_SOURCE -Wall -ggdb -std=c99 -O0
CPPFLAGS=-Wall -ggdb

LIBS=-lbz2 -lexpat -lstdc++ -lxapian

.PHONY: default clean

default: bzmain wkmain

bzmain: bzmain.o bzextract.o bzpartial.o
	gcc $^ -o $@ $(LIBS)

wkmain: bzpartial.o wkparse.o wkmain.o wkindex.o bzextract.o
	gcc $^ -o $@ $(LIBS)

clean:
	rm -f *.o a.out main bzmain wkmain
