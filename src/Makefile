CFLAGS=-D_BSD_SOURCE -D_LARGEFILE64_SOURCE -Wall -ggdb -std=c99 -O2 -fPIC
CXXFLAGS=-Wall -std=gnu++0x -ggdb -fPIC

LIBS=-L. -lbz2 -lexpat -lstdc++ -lxapian

.PHONY: all clean

all: libbzwiki.so.1 bzmain wkmain

libbzwiki.so.1: bzpartial.o wkparse.o wkmain.o wkindex.o bzextract.o
	gcc $^ -shared -Wl,-soname,libbzwiki.so.1 -o $@ 

bzmain: bzmain.o bzextract.o bzpartial.o
	gcc $^ -o $@ $(LIBS)

wkmain: wkmain.o bzpartial.o wkparse.o wkindex.o bzextract.o
	gcc $^ -o $@ $(LIBS)

clean:
	rm -f *.o a.out main bzmain wkmain *.so *.so.1 gmon.out callgrind.*
	rm -fr db/
