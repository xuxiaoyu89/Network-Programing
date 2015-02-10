

CC = gcc

LIBS = -lm -lresolv -lsocket -lnsl -lpthread /home/courses/cse533/Stevens/unpv13e_solaris2.10/libunp.a

FLAGS = -g -O2

CFLAGS = ${FLAGS} -I/home/courses/cse533/Stevens/unpv13e_solaris2.10/lib

all: server client


get_ifi_info_plus.o: get_ifi_info_plus.c
	${CC} ${CFLAGS} -c get_ifi_info_plus.c

rtt_plus.o: rtt_plus.c
	${CC} ${CFLAGS} -c rtt_plus.c

dg_send_recv.o: dg_send_recv.c rtt_plus.c
	${CC} ${CFLAGS} -c dg_send_recv.c rtt_plus.c

port_dg_send_recv.o: port_dg_send_recv.c rtt_plus.c
	${CC} ${CFLAGS} -c port_dg_send_recv.c rtt_plus.c

server: server.o get_ifi_info_plus.o rtt_plus.o port_dg_send_recv.o
	${CC} ${FLAGS} -o server server.o get_ifi_info_plus.o port_dg_send_recv.o rtt_plus.o ${LIBS}

server.o: server.c
	${CC} ${CFLAGS} -c server.c

client: client.o get_ifi_info_plus.o rtt_plus.o dg_send_recv.o 
	${CC} ${FLAGS} -o client client.o get_ifi_info_plus.o dg_send_recv.o rtt_plus.o ${LIBS}
client.o: client.c
	${CC} ${CFLAGS} -c client.c

clean:
	server server.o client client.o get_ifi_info_plus.o port_dg_send_recv.o dg_send_recv.o rtt_plus.o
