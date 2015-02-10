

CC = gcc

LIBS = -lm -lpthread /home/users/cse533/Stevens/unpv13e/libunp.a

FLAGS = -g -O2

CFLAGS = ${FLAGS} -I/home/users/cse533/Stevens/unpv13e/lib


all: arp tour



get_hw_addrs.o: get_hw_addrs.c
	${CC} ${CFLAGS} -c get_hw_addrs.c

tour.o: tour.c get_hw_addrs.c
	${CC} ${CFLAGS} -c tour.c get_hw_addrs.c

tour: tour.o get_hw_addrs.o
	${CC} ${FLAGS} -o tour tour.o get_hw_addrs.o ${LIBS}

arp.o: arp.c get_hw_addrs.c
	${CC} ${CFLAGS} -c arp.c get_hw_addrs.c

arp: arp.o get_hw_addrs.o
	${CC} ${FLAGS} -o arp arp.o get_hw_addrs.o ${LIBS}


clean:
	get_hw_addrs.o arp arp.o tour tour.o