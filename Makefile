CC = gcc
AR = ar
C_FLAGS = -Wall -O2 -c -I include -static -m64

all: build live

build:
	$(CC) $(C_FLAGS) term/uterm.c -o term/uterm.o
	$(CC) $(C_FLAGS) term/embfonts.c -o term/embfonts.o

	$(AR) -rsv libuterm.a term/uterm.o term/embfonts.o

live:
	$(CC) -Wall -O2 -I include main.c -o main -lX11 -L. -luterm

.PHONY: clean
clean:
	rm -f term/uterm.o term/embfonts.o libuterm.a main