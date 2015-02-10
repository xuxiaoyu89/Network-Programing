#include "unp.h"
#include "hw_addrs.h"

#define MAXLINE 4096
#define ODRPATH "myodrpath"

static struct sockaddr_un odraddr;

void msg_send(int sockfd, char *desaddr, int des_portnum, char * src_port, char *sendmsg, int redis_flag)
{
    int sendnum;
    
    char msg_seq[36];  //client把信息存在msg_seq中发给odr
    int desIP;
    
    //printf("in msg_send\n");
    desIP = (int) inet_addr(desaddr);
    //    printf("desIP: %d\n", desIP);
    
    //client - ODR message
    memcpy((void *)msg_seq, (void *)&desIP, 4);
    memcpy((void *)(msg_seq + 4), (void *)&des_portnum, 4);
    memcpy((void *)(msg_seq + 8), (void *)&redis_flag, 4);
    memcpy((void *)(msg_seq + 12), (void *)src_port, 20);
    memcpy((void *)(msg_seq + 32), (void *)sendmsg, 4);
    
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
    
    //肯定都是本地得ODR发送得，所以recvfrom的后2个参数并不重要了
    recvnum = recvfrom(sockfd, msg_seq, MAXLINE, 0, NULL, NULL);
    if (recvnum < 0){
        printf("recv error, errno: %d\n", errno);
        return;
    }
    //printf("recvnum: %d", recvnum);
    
    //server - ODR
    srcIP.s_addr = *((int *)msg_seq);           //IP
    strcpy(srcaddr, inet_ntoa(srcIP));
    *src_portnum = *((int *)(msg_seq + 4));     //port number
    strcpy(recvmsg, msg_seq + 8);               //string message
    recvmsg[4] = '\0';
    
}

int main(int argc, char **argv){
    
    struct hwa_info * myhwa_info, *current;
    int primIP;
    char serverIP[20];
    struct in_addr sinaddr;
    struct in_addr cinaddr;
    struct hostent * host;
    //create a domain socket;
    int sockfd;
    struct sockaddr_un servaddr;
    char servpath[20] = "permenant_serverpath";
    
    
    char recvmsg[MAXLINE];
    char recvIPclient[20];
    int recvcliport;
    servpath[20] = '\0';
    
    
    
    //get the IP of the node;
    myhwa_info = get_hw_addrs();
    current = myhwa_info;
    
    while(current != NULL){
        if(strcmp(current->if_name, "eth0") == 0){
            primIP = (((struct sockaddr_in *)(current->ip_addr))->sin_addr).s_addr;
            sinaddr.s_addr = primIP;
            strcpy(serverIP, inet_ntoa(sinaddr));
            break;
        }
        current = current->hwa_next;
    }
    
    printf("primIP: %s\n", serverIP);
    
    
    
    
    
    unlink(servpath);
    sockfd = socket(AF_LOCAL, SOCK_DGRAM, 0);
    
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sun_family = AF_LOCAL;
    strcpy(servaddr.sun_path, servpath);
    
    //bind sockfd and servaddr;
    bind(sockfd, (SA *)&servaddr, sizeof(servaddr));
    
    bzero(&odraddr, sizeof(odraddr));
    odraddr.sun_family = AF_LOCAL;
    strcpy(odraddr.sun_path, ODRPATH);

    for(;;){
        
        memset(recvmsg, 0, MAXLINE);
        memset(recvIPclient, 0, 20);
        
        
        msg_recv(sockfd, recvmsg, recvIPclient, &recvcliport);
        
        cinaddr.s_addr = inet_addr(recvIPclient);
        
        
        
        
        host = gethostbyaddr((char *)&sinaddr, sizeof(sinaddr), AF_INET);
        printf("Server at %s", host->h_name);
        host = gethostbyaddr((char *)&cinaddr, sizeof(cinaddr), AF_INET);
        
        printf(" responding to request from %s\n", host->h_name);
        
        
        printf("the msg from client: %s\n", recvmsg);
        printf("the portnum of the client: %d\n", recvcliport);
        
        
        msg_send(sockfd, recvIPclient, recvcliport, servpath, recvmsg, 0);
        //print something;
        //printf("server have send back the msg to client!\n");
    }
}




















