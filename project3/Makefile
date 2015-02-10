

CC = gcc

LIBS = -lm -lpthread /home/users/cse533/Stevens/unpv13e/libunp.a

FLAGS = -g -O2

CFLAGS = ${FLAGS} -I/home/users/cse533/Stevens/unpv13e/lib


all: ODR_cse533-12 client_cse533-12 server_cse533-12



get_hw_addrs.o: get_hw_addrs.c
	${CC} ${CFLAGS} -c get_hw_addrs.c

ODR_cse533-12.o: ODR_cse533-12.c get_hw_addrs.c
	${CC} ${CFLAGS} -c ODR_cse533-12.c get_hw_addrs.c

client.o: client.c get_hw_addrs.c
	${CC} ${CFLAGS} -c client.c get_hw_addrs.c

client_cse533-12: client.o get_hw_addrs.o
	${CC} ${FLAGS} -o client_cse533-12 client.o get_hw_addrs.o ${LIBS}

server.o: server.c get_hw_addrs.c
	${CC} ${CFLAGS} -c server.c get_hw_addrs.c

server_cse533-12: server.o get_hw_addrs.o
	${CC} ${FLAGS} -o server_cse533-12 server.o get_hw_addrs.o ${LIBS}

ODR_cse533-12: ODR_cse533-12.o get_hw_addrs.o
	${CC} ${FLAGS} -o ODR_cse533-12 ODR_cse533-12.o get_hw_addrs.o ${LIBS}


clean:
	ODR_cse533-12 ODR_cse533-12.o get_hw_addrs.o client.o client_cse533-12 server_cse533-12 server.o