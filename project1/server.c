#include "unp.h"

#define MAXLINE 4096
#define SA struct sockaddr
#define LISTENQ 1024           

static void * timeservice(void * paras){
    char buff[MAXLINE];
    int connfd = *(int *)paras;
    for(;;){
        time_t ticks = time(NULL);
        snprintf(buff, sizeof(buff), "%.24s\r\n", ctime(&ticks));
        
        
        /*
         check write function: if the return value is < 0, and the error is EPIPE,
         it means the client has been terminated. Then we print out some message, and
         terminate the thread.
        */
        int nwritten;
        if( (nwritten = write(connfd, buff, strlen(buff)) ) < 0 ){
            if(errno == 0){
                printf("The served time client has been terminated: EPIPE detected.\n");
                break;
            }
        }
        
        //else, the write function returns a number > 0, sleep for 5 seconds, and write time again.
        sleep (5);
    }
    
    /*
     situation: time client ^C, after that there comes an echo client, 
     the data in buff here will be sent to the echo client next time.
     so I clear the buff. Notice that the buff here is reused in different threads..........
     */
    memset(buff, 0, MAXLINE);
    
    close(connfd);
    pthread_exit(NULL);
}


static void * echoservice(void * paras){
    
    char buff[MAXLINE];
    int nread;
    int connfd = *(int *)paras;
    
    /*
     the read function returns a value > 0, meaning that there is something to write;
     then write data to the connfd;
     */
    while( (nread = read(connfd, buff, sizeof(buff)) ) > 0 ){
        write(connfd, buff, strlen(buff));
        memset(buff, 0, MAXLINE);
    }
    
    /*
     read() gets a EOF(the user typed ^D), meanning the echoclient has been terminated;
     then print some message, terminate the process.
     */
    if(nread == 0){
        printf("The served echo client has been terminated: EOF detected.\n");
    }
    
    if(nread < 0){
        printf("read error from echoservice.\n");
    }
    
    memset(buff, 0, MAXLINE);
    close(connfd);
    pthread_exit(NULL);
}




int main(int argc, char **argv){                                        //main function;

    int listenfd[2], *connfd;
    struct sockaddr_in servaddr[2];
    char buff[MAXLINE];
    time_t ticks;
    int maxfdp;
    struct fd_set fds;                                                  //file discriptors in select();
    
    
    /*
     After the time client is shutted down(^C), it send server a FIN and closes all its sockets.
     At this time, the server can still send data to the client. The first time the data will cause a RST
     from client, and the second time the data will cause a SIGPIPE, which will terminate the server process.
     So here we have to handle the SIGPIPE signal(ignore it).
    */
    signal(SIGPIPE, SIG_IGN);                                           
    

    int i,j;
    const int optval = 1;
    
        
    

    for (i=0; i<2; i++) {                                           //set up two sockets for time and echo service.
        listenfd[i] = socket(AF_INET, SOCK_STREAM, 0);                  //socket();
        /*
         non-block socket
         */
        //fcntl(listenfd[i], F_SETFL, O_NONBLOCK);
        bzero(&servaddr[i], sizeof(servaddr[i]));
        servaddr[i].sin_family = AF_INET;
        servaddr[i].sin_addr.s_addr = htons(INADDR_ANY);
        servaddr[i].sin_port = htons(60000+i);
        /*
         SO_REUSEADDR: server initiate the termination of the connection, but TCP keeps the connection open in the TIME_WAIT
         state for 2 MSLs.
         */
        // write setsockopt() here!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        setsockopt(listenfd[i], SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
        bind(listenfd[i], (SA *)&servaddr[i], sizeof(servaddr[i]));     //bind();
        listen(listenfd[i],LISTENQ);                                    //listen();
    }
    
    pthread_t tid;                                                      //??????????detach(tid), only one thread id????????
    
    for(;;){                                                            //infinite loop for select();
        FD_ZERO(&fds);                                                  //empty the set;
        FD_SET(listenfd[0],&fds);
        FD_SET(listenfd[1],&fds);
        maxfdp = listenfd[0]>listenfd[1]?listenfd[0]+1:listenfd[1]+1;   //set maxfdp;
        
        switch (select(maxfdp, &fds, NULL, NULL, NULL)) {               //select(), timeval = null, block here.
            case -1:
                printf("select error.\n");
                exit(0);
                
            default:
                for(j=0; j<2; j++){
                    if(FD_ISSET(listenfd[j], &fds)){
                    
                        connfd = (int*) malloc(sizeof(int));
                        *connfd=accept(listenfd[j], (SA *) NULL, NULL);
                        
                        int test;
                        if(j == 0){
                            test = pthread_create(&tid, NULL, timeservice, connfd); //create a new thread, serve time client;
                        }
                        else{
                            test = pthread_create(&tid, NULL, echoservice, connfd); //create a new thread, serve echo client;
                        }
                        pthread_detach(tid);
                    }
                }
                
                break;
        }
        
    }
    
}





















