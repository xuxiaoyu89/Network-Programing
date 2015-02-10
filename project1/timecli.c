#include "unp.h"

#define MAXLINE 4096
#define SA struct sockaddr

int main(int argc, char **argv){
    /*
     send message to parent, indicating that the xterm is processing....
     */
    
    
    /*
     here every message which user needs to read, needs to be sent through 
     pipe. Here, 3 is for pipe's write end(dup2 function), because if it is
     showed here, it will process to exit(0), then the message will flash and
     disappear.
     */
    char buff[MAXLINE];
    strcpy(buff, "xterm processing......");
    write(3, buff, strlen(buff)+1 );
    memset(buff,0,MAXLINE);
    
    
    int sockfd, readn;
    char recvline[MAXLINE + 1];
    struct sockaddr_in servaddr;

    //argc != 2, shows that no ip address is comming in....
    if(argc != 2){
        strcpy(buff, "IP address missing...");
        write(3, buff, strlen(buff)+1 );
        memset(buff,0,MAXLINE);
        exit(0);
    }
    
    //socket(); error handle;
    if( (sockfd = socket(AF_INET,SOCK_STREAM,0)) < 0){
        strcpy(buff, "socket error...");
        write(3, buff, strlen(buff)+1 );
        memset(buff,0,MAXLINE);
        exit(0);
    }
    
    //set the address of the server, for client to connect server;
    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(60000);
    
    //presentation to numeric;
    if(inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0){
        strcpy(buff, "inet_pton error...");
        write(3, buff, strlen(buff)+1 );
        memset(buff,0,MAXLINE);
        exit(0);
    }
    
    //connect(); error handle;
    int test_conn = connect(sockfd, (SA *) &servaddr, sizeof(servaddr));
    if(test_conn < 0){
        strcpy(buff, "can not connect server...");
        write(3, buff, strlen(buff)+1 );
        memset(buff,0,MAXLINE);
        exit(0);
    }
                     

    //read(), read from sockfd;
    while ( (readn = read(sockfd,recvline,MAXLINE)) >0) {
        recvline[readn] = 0;
        if(fputs(recvline,stdout) == EOF){
            strcpy(buff, "fputs error...");
            write(3, buff, strlen(buff)+1 );
            memset(buff,0,MAXLINE);
            exit(0);
        }
    }
    
    //server terminated, so a FIN(EOF) is send, read() returns 0;
    //Here we inform the user that server has been terminated;
    if(readn == 0){
        strcpy(buff, "the server terminated, read() returns 0.");
        write(3, buff, strlen(buff)+1 );
        memset(buff,0,MAXLINE);
        exit(0);
    }
    
    //read error;
    if(readn<0){
        strcpy(buff, "read error...");
        write(3, buff, strlen(buff)+1 );
        memset(buff,0,MAXLINE);

        exit(0);
    }
    exit(0);
}
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    

