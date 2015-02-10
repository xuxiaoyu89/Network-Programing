# This is a sample Makefile which compiles source files named:
# - server.c
# - client.c
# - timecli.c
# - echocli.c
# and creating executables: "server", "client", "timecli"
# and "echocli", respectively.
#
# It uses various standard libraries, and the copy of Stevens'
# library "libunp.a" in ~cse533/Stevens/unpv13e_solaris2.10 .
#
# I did not pick up the readline function from stevens code, but it runs correctly.


CC = gcc

LIBS = -lresolv -lsocket -lnsl -lpthread /home/courses/cse533/Stevens/unpv13e_solaris2.10/libunp.a

FLAGS = -g -O2

CFLAGS = ${FLAGS} -I/home/courses/cse533/Stevens/unpv13e_solaris2.10/lib

all: client server echocli timecli


timecli: timecli.o
	${CC} ${FLAGS} -o timecli timecli.o ${LIBS}
timecli.o: timecli.c
	${CC} ${CFLAGS} -c timecli.c


echocli: echocli.o
	${CC} ${FLAGS} -o echocli echocli.o ${LIBS}
echocli.o: echocli.c
	${CC} ${CFLAGS} -c echocli.c


server: server.o
	${CC} ${FLAGS} -o server server.o ${LIBS}
server.o: server.c
	${CC} ${CFLAGS} -c server.c


client: client.o
	${CC} ${FLAGS} -o client client.o ${LIBS}
client.o: client.c
	${CC} ${CFLAGS} -c client.c



clean:
	rm echocli echocli.o server server.o client client.o timecli timecli.o
