/*
 define the message formation
 ----------------------------------------------------------
 | client - ODR             ||      server - ODR            |
 | 1-4:  IP                 ||      1-4:  IP                |
 | 5-8:  port number        ||      4-8:  port number       |
 | 9-12: flag               ||      8- :  string message    |
 | 12- : string message     ||                              |
 -----------------------------------------------------------
*/


#include "unp.h"
#include <setjmp.h>
#include "hw_addrs.h"

#define ODRPATH "myodrpath\0"

static struct sockaddr_un odraddr;
static sigjmp_buf jmpbuf;
static int timeoutflag;
static int x;
static char input[5];

void msg_send(int sockfd, char *desaddr, int des_portnum, char *sendmsg, int redis_flag)
{
    int sendnum;
    
    char msg_seq[36]; 
    int desIP;
    
    //printf("in msg_send\n");
    desIP = (int) inet_addr(desaddr);
    
    //client - ODR message
    memcpy((void *)msg_seq, (void *)&desIP, 4);
    memcpy((void *)(msg_seq + 4), (void *)&des_portnum, 4);
    memcpy((void *)(msg_seq + 8), (void *)&redis_flag, 4);
    memcpy((void *)(msg_seq + 12), (void *)sendmsg, 4);
    
    sendnum = sendto(sockfd, msg_seq, sizeof(msg_seq), 0, (SA *)&odraddr, sizeof(odraddr));
    if (sendnum < 0){
        printf("send error, errno: %d\n", errno);
        return;
    }
    //printf("sendnum: %d\n", sendnum);
}


void msg_recv(int sockfd, char *recvmsg, char *srcaddr, int *src_portnum)
{
    int recvnum;
    
    char msg_seq[MAXLINE];
    
    struct in_addr srcIP;
    
    //printf("int msg_recv\n");
    
    recvnum = recvfrom(sockfd, msg_seq, MAXLINE, 0, NULL, NULL);
    if (recvnum < 0){
        printf("recv error, errno: %d\n", errno);
        return;
    }
    //printf("recvnum: %d\n", recvnum);
    
    //server - ODR
    srcIP.s_addr = *((int *)msg_seq);           //srcIP
    strcpy(srcaddr, inet_ntoa(srcIP));
    *src_portnum = *((int *)(msg_seq + 4));     //src port number
    strcpy(recvmsg, msg_seq + 8);               //string message
    recvmsg[4] = '\0';
}

static void sig_alrm(int signo){
    siglongjmp(jmpbuf, 1);
}

int main(int argc, char **argv)
{
    
    struct hwa_info * myhwa_info, *current;
    int sockfd;
    struct sockaddr_un cliaddr;
    char template[20];
    int pathnum;
    int sendnum;
    int des_portnum;
    int redis_flag;
    char answer[3];

    char msg[4] = "abcd";   //msg to be sent;
    char recvmsg[4];        //msg recvd;
    char srcaddr[20];       //presentation form of server IP address(used in msg_recv());
    int src_portnum;        //portnum of the server;
    
    char dest[15];          //presentation form of server IP address(used in msg_send());
    int primIP;
    char IP[20];
    struct hostent * clihost, *servhost;
    
//    char input[5];
    
    struct in_addr cli_inaddr, serv_inaddr;
    
    //get the IP of the node;
    myhwa_info = get_hw_addrs();
    current = myhwa_info;
    
    while(current != NULL){
        if(strcmp(current->if_name, "eth0") == 0){
            primIP = (((struct sockaddr_in *)(current->ip_addr))->sin_addr).s_addr;
            cli_inaddr.s_addr = primIP;
            strcpy(IP, inet_ntoa(cli_inaddr));
            break;
        }
        current = current->hwa_next;
    }
    
    printf("primIP: %s\n", IP);
    cli_inaddr.s_addr = primIP;
    clihost = gethostbyaddr((char *)&cli_inaddr, sizeof(cli_inaddr), AF_INET);
    printf("the client host name: %s\n", clihost->h_name);
    
    //socket();
    sockfd = socket(AF_LOCAL, SOCK_DGRAM, 0);
    
    //initialize the cliaddr;
    
    bzero(&cliaddr, sizeof(cliaddr));
    cliaddr.sun_family = AF_LOCAL;
    strcpy(template, "/tmp/template_XXXXXX");
    pathnum = mkstemp(template);
    if (pathnum < 0){
        printf("mkstemp cannot create file, errno is: %d\n", errno);
        exit(1);
    }
    unlink(template);
    strcpy(cliaddr.sun_path, template);
    printf("sun_path: %s\n", cliaddr.sun_path);
    bind(sockfd, (SA *)&cliaddr, sizeof(cliaddr));
    
    //initialize the odraddr
    bzero(&odraddr, sizeof(odraddr));
    odraddr.sun_family = AF_LOCAL;
    strcpy(odraddr.sun_path, ODRPATH);
    
    //timeout signal;
    signal(SIGALRM, sig_alrm);
    
    for(;;){
        //ask user for vmx
        printf("input vm1, vm2, ... vm10 to choose a server node: \n");
        scanf("%s", input);
        printf("input: %s\n", input);
        servhost = gethostbyname(input);
        serv_inaddr.s_addr = *((int *)servhost->h_addr_list[0]);
        strcpy(dest, inet_ntoa(serv_inaddr));
        printf("the server host addr: %s\n", dest);
        
        
        //ask the user if want to rediscovery;
        printf("do you want to rediscovery the route?(yes/no)\n");
        scanf("%s", answer);
        if(strcmp(answer, "yes") == 0){
            redis_flag = 1;
        }
        else if(strcmp(answer, "no") == 0){
            redis_flag = 0;
        }
        else{
            printf("input error!\n");
            continue;
        }
        
        
        //call msg_send() function
        des_portnum = 0;
        strcpy(msg, "abcd");
        printf("Client at %s sending request to server at %s\n", clihost->h_name, input);
        msg_send(sockfd, dest, des_portnum, msg, redis_flag);
        
        //alarm();
        timeoutflag = 0;
        alarm(5);
        if(sigsetjmp(jmpbuf, 1) != 0){
            if(timeoutflag != 0){
                printf("second timeout, give up!\n");
                continue;
            }
            printf("timeoutflag: %d\n", timeoutflag);
            timeoutflag = 1;
            redis_flag = 1;
            printf("Client at %s timeout on response from %s\n", clihost->h_name, input);
            msg_send(sockfd, dest, des_portnum, msg, redis_flag);
            
            
            alarm(5);
        }
        //call msg_recv() function
        msg_recv(sockfd, recvmsg, srcaddr, &src_portnum);
        alarm(0);
        printf("Client at %s received from %s\n", clihost->h_name, input);
        printf("timestamp: %d\n", time(NULL));
        printf("echoed msg from server: %s\n", recvmsg);
        printf("the IP address of server: %s\n", srcaddr);
        //printf("the port num of server: %d\n", src_portnum);

    }
    
    exit(0);
}






















