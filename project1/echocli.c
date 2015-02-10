#include "unp.h"

#define MAXLINE 4096
#define SA struct sockaddr

int main(int argc, char **argv){
    
    /*
     send message to parent;
     */
    char buff[MAXLINE];
    strcpy(buff, "xterm processing......");
    write(3, buff, strlen(buff)+1 );
    memset(buff, 0, MAXLINE);

    
    int sockfd, readn;
    struct sockaddr_in servaddr;
    buff[100];
    
    /*select between stdin and sockfd:
     stdin: user input, needs to be echoed;
     sockfd: the message from server, either echoed message or FIN;
     */
    struct fd_set fds;
    
    
    if(argc != 2){
        strcpy(buff, "IP address not available...");
        write(3, buff, strlen(buff)+1 );
        memset(buff, 0, MAXLINE);
        exit(0);
    }
    
    if( (sockfd = socket(AF_INET,SOCK_STREAM,0)) < 0){
        strcpy(buff, "socket error...");
        write(3, buff, strlen(buff)+1 );
        memset(buff, 0, MAXLINE);
        exit(0);
    }
    
    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(60001);
    
    if(inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0){
        strcpy(buff, "inet_pton error...");
        write(3, buff, strlen(buff)+1 );
        memset(buff, 0, MAXLINE);
        exit(0);
    }
        
    int test_conn = connect(sockfd, (SA *) &servaddr, sizeof(servaddr));
                     
    if(test_conn < 0){
        strcpy(buff, "can not connect server...");
        write(3, buff, strlen(buff)+1 );
        memset(buff, 0, MAXLINE);
        exit(0);
    }
    
    /*
     handle the message from the last timeservice.
     */
    int sockbufsize = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (void *)&sockbufsize, sizeof(int));
    sockbufsize = 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (void *)&sockbufsize, sizeof(int));
    /*
     from here, scan the input from user. 
     */
    printf("now you can input something to the server: \n");
    
    int eofflag = 0;
    while (1) {
        FD_ZERO(&fds);
        FD_SET(fileno(stdin), &fds);
        FD_SET(sockfd, &fds);
        int maxfdp;
        maxfdp = fileno(stdin)>sockfd?fileno(stdin)+1:sockfd+1;
        switch (select(maxfdp, &fds, NULL, NULL, NULL)) {
            case -1:
                printf("select error.\n");
                exit(0);
                break;
            default:
                if(FD_ISSET(fileno(stdin), &fds)){
                    if(scanf("%s", buff) != EOF){
                        write(sockfd, buff, strlen(buff));
                    }
                    else{
                        eofflag = 1;
                    }
                }
                if (FD_ISSET(sockfd, &fds)) {
                    int readn;
                    readn = read(sockfd, buff, MAXLINE);
                    if(readn == 0){
                        strcpy(buff, "server has been crashed!!!!!!");
                        write(3, buff, strlen(buff)+1 );
                        memset(buff, 0, MAXLINE);
                        exit(0);
                    }
                    else if (readn > 0){
                        printf("%s\n", buff);
                    }
                    else {
                        strcpy(buff, "read error.");
                        write(3, buff, strlen(buff)+1 );
                        memset(buff, 0, MAXLINE);
                        exit(0);
                    }
                }
                break;
                
        }
        if(eofflag == 1){
            break;
        }
    }

    exit(0);
}
    
    
  

