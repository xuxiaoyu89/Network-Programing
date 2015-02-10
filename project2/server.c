//check if the IPclient is local or not;
//adding reliability: server receiving file_name, and sending port number;



#include "unp.h"
#include "unpifiplus.h"
#include "unprtt_plus.h"
#include <setjmp.h>



/*
 this struct holds all the information of the socket: 
 sockfd;
 IP address bound to the socket;
 network mask for the IP address;
 subnet address;
 */


struct mysocks {
    int sockfd;                    //the sockfd;
    struct sockaddr_in addr;                       //sockaddr bound to the socket
    struct sockaddr_in maddr;                      //mask address in a sockaddr_in
    struct sockaddr_in subnetaddr;
    char *IPADDR;                  //IP address bound to the socket;
    char *mask;                    //network mask for the IP address;
    char *subnet;                  //subnet address;
    struct mysocks *nextsocket;
};

struct client{
    SA IPclient;     //the address of client;
    int pid;                          //the process number of the child process;
    struct client * nextclient;
};

struct myHdr{
    uint32_t seq;
    uint32_t ts;
    int segmtNum;
    int adSize;
    int fin;             //fin: 1 when end of file;
                         //     2 when want to probe;
};

struct node{
    int segmt;
    struct myHdr sendhdr;
    char data[450];
    struct msghdr sendmsg;
    struct iovec iovsend[2];
    struct node * next;
};

static struct client * firstclient;
static struct client * currentclient, * cursorclient;
static int clientnum;

static struct node header;
static struct node * tail, *cursor;

static int maxwindowsize;
static int cwnd;
static int adSize;
static int ssthresh;
static int dupACKcount;                 //count the duplicate ACK;
static int normalcount;                 //cwnd > ssthresh, no duplicate, normalcount ++;
static int readfinflag;                 //if we get the last segment from the file, readfinflag = 1;
static int ackfinflag;                  //if we get the ack of the last segment, ackfinflag = 1;
static int lastsent;                    //last segment in the sending list;
static int lastseg;                     //the last segment of the file;
static int lastacked;                   //the last segment which get acked in the list;

static struct rtt_info rttinfo;
static sigjmp_buf jmpbuf;
static int rttinit = 0;
struct itimerval val_itimer;
//const struct itimerval *p_val_itimer=&val_itimer;


/*********timeout: sig_alarm()*********/
static void sig_alrm(int signo){
    printf("timeout!!!!!!!!!!!\n");
    siglongjmp(jmpbuf, 1);
}


/*********timeout: sendagain()*********/
sendagain(int connfd){
    //printf("sendagain\n");
    struct  node *p;
    p=header.next;
//    if (p->next==NULL){
//        sendmsg(connfd, &(p->sendmsg), 0);
//    }
//    else{
        while( p->segmt <= lastacked ){
            p=p->next;
        }
        int sendnum = sendmsg(connfd, &(p->sendmsg), 0);
        printf("resend: %d\n", p->segmt);
//    }
}

/**************************************/
void sendmissedseg(int missedsegment, int connfd){
    cursor = header.next;
    while ( cursor != NULL )
    {
        if(cursor->segmt == missedsegment){
            int sendnum = sendmsg(connfd, &(cursor->sendmsg), 0);
            if(sendnum < 0){
                printf("sendfile error, errno is: %d\n", errno);
                exit(0);
            }
            printf("missted segment resent: %d\n", cursor->segmt);
            break;
        }
        cursor = cursor->next;
    }
}

/************************************************** readFile() *************************************************/
void updatenodes(int *ch, FILE *fp, int count) //head is the tail of the current list
{
    //delete all the nodes;
    cursor = header.next;
    while(cursor != NULL){
        header.next = cursor->next;
        free(cursor);
        cursor = header.next;
    }
    
    //add new nodes;
    struct node * temp;
    cursor = &header;
    int i, j;
    for ( i=0; i<count; i++ )
    {
        cursor->next = malloc(sizeof(struct node));
        temp = cursor->next;
        memset(temp, 0, sizeof(struct node));
        
        /*if we have reached the end of file, we should stop reading;
          another thing we should do is that we set fin to 1;
         */
        for ( j=0; j<450; j++ )
        {
            (temp->data)[j] = *ch;
            *ch = fgetc(fp);
            if(*ch == EOF){
                printf("from readFile: get EOF!\n");
                (temp->sendhdr).fin = 1;
                readfinflag = 1;
                break;
            }
        }
        
        if(readfinflag == 0){
            (temp->sendhdr).fin = 0;
        }
        
        //printf("data: %s\n", temp->data);
        //printf("======================================\n");
        temp->segmt = lastsent + i + 1;
        
        (temp->sendhdr).segmtNum = lastsent + i + 1;
        (temp->iovsend)[0].iov_base = (char *)&(temp->sendhdr);
        (temp->iovsend)[0].iov_len = sizeof(struct myHdr);
        (temp->iovsend)[1].iov_base = temp->data;
        (temp->iovsend)[1].iov_len = 450;
        
        (temp->sendmsg).msg_iov = temp->iovsend;
        (temp->sendmsg).msg_iovlen = 2;
        
        (temp->sendmsg).msg_name = NULL;
        (temp->sendmsg).msg_namelen = 0;
        
        cursor = cursor->next;
        if(readfinflag == 1){
            break;
        }
    }
    
}

/************************************************* dg_send_send() *******************************************************/
void dg_send_send(int connfd) //head is the tail before we update the list;
{
    cursor = header.next;
    while ( cursor != NULL )    //让cursor指向最后一个node
    {
        int sendnum = sendmsg(connfd, &(cursor->sendmsg), 0);
        if(sendnum < 0){
            printf("sendfile error, errno is: %d\n", errno);
            exit(0);
        }
        lastsent = cursor->segmt;
        
        //printf("sendnum: %d\n", sendnum);
        //printf("segment sent: %d\n", cursor->segmt);
        //print a message if EOF
        if((cursor->sendhdr).fin == 1){
            printf("this is the EOF segment!\n");
            lastseg = lastsent;
        }
        tail = cursor;
        cursor = cursor->next;
    }
    
}

/********************************************** readACK() ******************************************************/
int readACK(int connfd){
    struct msghdr ACK;
    struct iovec ackvec[1];
    struct myHdr ackhdr;

    while(1){
        
        memset(&ACK, 0, sizeof(struct msghdr));
        
        memset(&ackhdr, 0, sizeof(struct myHdr));
        
        ACK.msg_name = NULL;
        ACK.msg_namelen = 0;
        ACK.msg_iov = ackvec;
        ACK.msg_iovlen = 1;
        ackvec[0].iov_base = (char *)&ackhdr;
        ackvec[0].iov_len = sizeof(struct myHdr);
        
        int recvnum = recvmsg(connfd, &ACK, 0);

        if(recvnum < 0){
            
            printf("read ack error, errno: %d\n", errno);
            exit(0);
        }
        
        rtt_newpack(&rttinfo);
        
        printf("acked #: %d| adwindowsize: %d| \n", ackhdr.segmtNum, ackhdr.adSize);
        
        
        //update the cwnd;
        //get all acks for the whole file;
        if(ackhdr.segmtNum == lastseg){
            ackfinflag = 1;
            break;
        }
        //get all acks for the current list;
        if(ackhdr.segmtNum == lastsent){
            cwnd += (ackhdr.segmtNum - lastacked);
            adSize = ackhdr.adSize;
            lastacked = ackhdr.segmtNum;
            break;
        }
        //get a duplicated ack;
        if(ackhdr.segmtNum == lastacked){
            dupACKcount ++;
            sendmissedseg(ackhdr.segmtNum + 1, connfd);
        }
        //get a normal ack;
        else if(ackhdr.segmtNum > lastacked){
            dupACKcount = 0;
            cwnd += (ackhdr.segmtNum - lastacked);
            lastacked = ackhdr.segmtNum;
        }
    }
}

/************************************************* dg_send_send() *******************************************************/
void file_send_recv(int connfd, int * ch, FILE * fp){
    
    
    readfinflag = 0;
    ackfinflag = 0;
    lastsent = 0;
    lastacked = 0;
    
    //initialize the alarm!!!!
    
    
    
    updatenodes(ch, fp, 1);
    tail = header.next;

/*timeout*/
    if (rttinit == 0)
    {
        rtt_init(&rttinfo);
        rttinit = 1;
        rtt_d_flag = 1;
    }
    signal(SIGALRM, sig_alrm);
    rtt_newpack(&rttinfo);
    (tail->sendhdr).ts = rtt_ts(&rttinfo);
/*timeout*/
    
    //here we should send the first segment;
    int sendnum = sendmsg(connfd, &(tail->sendmsg), 0);
    if(sendnum < 0){
        printf("first send error, errno: %d\n", sendnum);
        exit(0);
    }
    lastsent ++;
    cwnd = 1;
    while(1){
        
/*timeout*/
       // alarm(rtt_start(&rttinfo)/1000000);
        int second = rtt_start(&rttinfo)/1000000;
        int u_second = rtt_start(&rttinfo)%1000000;
        val_itimer.it_value.tv_sec = second;
        val_itimer.it_value.tv_usec=u_second;
        val_itimer.it_interval.tv_sec=0;
        val_itimer.it_interval.tv_usec=0;
        setitimer(0, &val_itimer, NULL);
        if ( sigsetjmp(jmpbuf, 1)!=0 ){
            if (rtt_timeout(&rttinfo)<0){
                printf("no response, give up!!!\n");
                rttinit = 0;
                exit(0);
            }
            sendagain(connfd);
            //alarm(rtt_start(&rttinfo)/1000000);
            int second = rtt_start(&rttinfo)/1000000;
            int u_second = rtt_start(&rttinfo)%1000000;
            val_itimer.it_value.tv_sec = second;
            val_itimer.it_value.tv_usec=u_second;
            val_itimer.it_interval.tv_sec=0;
            val_itimer.it_interval.tv_usec=0;
            setitimer(0, &val_itimer, NULL);
        }
/*timeout*/
        
        /***************** readACK() ******************/
        //in this function, we receive the ack from client, and update the segment list;
        //after calling this connection, tail points to the previous node of the new list;
        adSize = readACK(connfd);
        
/*timeout*/
        //alarm(0);
        val_itimer.it_value.tv_sec=0;
        val_itimer.it_value.tv_usec=0;
        val_itimer.it_interval.tv_sec=0;
        val_itimer.it_interval.tv_usec=0;
        setitimer(0, &val_itimer, NULL);
        rtt_stop(&rttinfo, rtt_ts(&rttinfo) - (tail->sendhdr).ts);
/*timeout*/
        
        if(adSize <= 0){
            
            rtt_newpack(&rttinfo);
            int second = rtt_start(&rttinfo)/1000000;
            int u_second = rtt_start(&rttinfo)%1000000;
            val_itimer.it_value.tv_sec = second;
            val_itimer.it_value.tv_usec=u_second;
            val_itimer.it_interval.tv_sec=0;
            val_itimer.it_interval.tv_usec=0;
            setitimer(0, &val_itimer, NULL);
            continue;
            
        }
        int readcount = adSize<cwnd?adSize:cwnd;
        
        if(ackfinflag == 1){
            printf("get the last ack from client!\n");
            break;
        }
        
        updatenodes(ch, fp, readcount);
        
/*timeout*/
        rtt_newpack(&rttinfo);
        (tail->sendhdr).ts = rtt_ts(&rttinfo);
/*timeout*/
        
        dg_send_send(connfd);
        //set alarm!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        
        printf("segment # sent up to: %d\n", tail->segmt);
        if(readfinflag == 1){
            continue;
        }
    }
}

/********************************************** dg_filetransmission() ******************************************************/
//the child function to handle one client.
void dg_filetransmission(int sockfd, char * file_name, SA * IPserver, SA *IPclient){
    
        
    //create a connection socket for data transmission;
    //create a new sockaddr_in for connfd;
    struct sockaddr_in connIPserver;
    bzero(&connIPserver, sizeof(connIPserver));
    connIPserver.sin_family = AF_INET;
    connIPserver.sin_port = htons(0);
    connIPserver.sin_addr.s_addr = ((struct sockaddr_in *)IPserver)->sin_addr.s_addr;
    
    
    int connfd;
    connfd = socket(AF_INET, SOCK_DGRAM, 0);            //create a new connection sockfd;
    bind(connfd, (SA *)&connIPserver, sizeof(connIPserver));               //bind this socket to IP address which get the request from client;
    //connect this connfd to client's IPclient and ephemeral port;
    
    
    //after bind, get the ephemeral port number of the new sockaddr;
    struct sockaddr_in bindedconnIPserver;
    int getsocknametest;
    int addrlen = sizeof(SA);
    getsocknametest = getsockname(connfd, (SA *)&bindedconnIPserver, &addrlen);
    //printf("getsockname result: %d\n", getsocknametest);
    
    
    int portlen;              //the length of servport;
    char servport[20];        //the decimel dotted servport number;
    sprintf(servport, "%d", bindedconnIPserver.sin_port);     //put port number in servport;
    portlen = strlen(servport);             //get the length of the servport;
    printf("the new port for the connfd is: %s\n", servport);

    /******************************* we connect the connfd to the client ********************************/
    int conn;
    conn = connect(connfd, IPclient, addrlen);
    if(conn < 0)
    {
        printf("connect error!\n");
        exit(0);
    }

    
    //send the port number of connfd to client from sockfd;
    //because the sockfd in the client is connected to the sockfd.
    //use dg_send_recv() to send port number and receive the ack;
    //here we pass servport(the portnum) and recACK(the received ack) to the function;
    char recACK[10];
    port_dg_send_recv(sockfd, connfd, (void *)servport, portlen, (void *)recACK, sizeof(recACK), IPclient, addrlen);
    printf("received ack: %s\n", recACK);
    
    /*************************************** file transimission ***************************************/
    //see if the received data is "ack";
    //if get ack, then close the listenning socket;
    //then we can begin to send file_name data to the client.
    if(strcmp(recACK, "ack") == 0){
        printf("closing listening socket.\n");
        close(sockfd);
    }
    
    
    /******************* read data from file_name *********************/
     
    int ch;
    
    FILE *fp;
    if ( (fp=fopen(file_name, "r"))==NULL )
    {
    printf("this file can not be opened!\n");
    exit(1);
    }
    
    ch = fgetc(fp);   //获取第一个字符
     
    //建立表头节点
    struct node *head;
    head = malloc(sizeof(struct node));
    head->segmt = 0;
    head->next = NULL;
    
    /************* file_send_recv(): send file and receive ack **************/
    printf("\n***************************** server starts to send data **********************************\n");
    file_send_recv(connfd, &ch, fp);
    
    //tell the parent process that we have finished the service of the current client.
    printf("child process ending...");
    char send[5];
    sprintf(send, "%d", getpid());
    write(11, send, 5);
    
    exit(0);
}

//*********************************************************************************************
// the function to handle SIGCHLD, no zombies;
void sig_chld(int signo){
    
    pid_t pid;
    int stat;
    
    while((pid = waitpid(-1, &stat, WNOHANG))>0){
        printf("child %d terminated\n", pid);
    }
    return;
}

//*********************************************************************************************
//the function to delete the client node when finish the service;
static void * deleteIPclient(){
    int readnum;
    char cpid[5];
    readnum = read(10, cpid, 5);
    int pid = atoi(cpid);
    
    //here delete the client struct;
    struct client * previousclient = firstclient;
    struct client * cursorclient;
    for(cursorclient = firstclient->nextclient; cursorclient != NULL; ){
        //printf("cursorclient->pid: %d\n", cursorclient->pid);
        //printf("int pid: %d\n", pid);
        
        if(cursorclient->pid == pid){
            printf("*************************************************************************************\n");
            printf("now stop serving the client with IP address: %s.\n", sock_ntop(&(cursorclient->IPclient), sizeof(SA)));
            previousclient->nextclient = cursorclient->nextclient;            //change the pointers;
            free(cursorclient);//free the memory;
            clientnum --;
            printf("now delete this client, and there are %d clients left.\n", clientnum);
            break;
        }
        previousclient = previousclient->nextclient;                          //move the cursor and previousclient to the next;
        cursorclient = cursorclient->nextclient;
    }
    
    pthread_exit(NULL);
}

//********************************************************************************************************************
//int main()
int main(int argc, char ** argv){
    
    /******************************** get parameter from server.in  ***********************************/
    char argument[2][20];
    int wnportnum;
    
    FILE * fp;
    fp = fopen("server.in", "r");
    char temp[20];
    int i = 0;
    for(i=0; i<2; i++){
        memset(temp, 0, 20);
        fgets(temp, 19, fp);
        strcpy(argument[i], temp);
    }
    
    wnportnum = atoi(argument[0]);
    maxwindowsize = atoi(argument[1]);
    //initialize the cwnd and ssthresh;
    cwnd = 1;
    ssthresh = 10;
    normalcount = 0;
    
    /****************************************** get the IP addresses of the server ******************************************/
    
    struct ifi_info *ifi, *ifihead;
    struct sockaddr_in *sa, *msa, subsa;   //sa and msa: pointer to the sockaddr_in struct in ifi; subsa: a sockaddr_in struct for subnetwork;
    u_char *ptr;
    char *subnet = malloc(20);                   //subnet address presentation, XX.XX.XX.XX;
    
    /*
     we store the socket information in a linked list.
    */
    struct mysocks *sockhead, *sockcurrent, *sockcursor;
    sockhead = malloc(sizeof(struct mysocks));
    sockcurrent = sockhead;
    int mysockssize = 0;
    
    
    //for select()
    struct fd_set fds;
    
    
    for(ifi = get_ifi_info_plus(AF_INET, 1); ifi != NULL;){
        sa = (struct sockaddr_in *)ifi->ifi_addr;
        msa = (struct sockaddr_in *)ifi->ifi_ntmaddr;
        /*printf("<");
        if(ifi->ifi_flags & IFF_UP) printf("up ");
        if(ifi->ifi_flags & IFF_BROADCAST) printf("BROADCAST ");
        if(ifi->ifi_flags & IFF_MULTICAST) printf("MULTICAST ");
        if(ifi->ifi_flags & IFF_LOOPBACK) printf("LOOPBACK ");
        if(ifi->ifi_flags & IFF_POINTOPOINT) printf("P2P ");
        printf(">\n");
         */
        //printf("%s\n", sock_ntop( (SA *)msa,sizeof(*msa)));
        //printf("%s\n", sock_ntop( (SA *)sa, sizeof(*sa)));
        
        char *sap = malloc(20);
        char *msap = malloc(20); 
        strcpy(sap, sock_ntop( (SA *)sa, sizeof(*sa)));               //sockaddr presentation, XX.XX.XX.XX;
        strcpy(msap, sock_ntop((SA *)msa, sizeof(*msa)));             //mask sockaddr presentation,XX.XX.XX.XX;
        

        subsa = *sa;
        subsa.sin_addr.s_addr = ((sa->sin_addr).s_addr)&((msa->sin_addr).s_addr);
        strcpy(subnet, sock_ntop( (SA *)&subsa, sizeof(subsa)));      //subnet sockaddr presentation,XX.XX.XX.XX;


        
        //int sockfd = socket(AF_INET, SOCK_DGRAM, 0);            //create a new sockfd;
        
        //for the sockaddr information;
        sockcurrent->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        bzero(&(sockcurrent->addr), sizeof(sockcurrent->addr));
        sockcurrent->addr.sin_addr = sa->sin_addr;
        //specify the port number for each sockaddr;
        sockcurrent->addr.sin_port = htons(wnportnum);
        sockcurrent->addr.sin_family = AF_INET;
        sockcurrent->maddr = *msa;
        sockcurrent->subnetaddr = subsa;
        
        //for the IP presentation information;
        sockcurrent->IPADDR = malloc(20);
        sockcurrent->mask = malloc(20);
        sockcurrent->subnet = malloc(20);
        strcpy(sockcurrent->IPADDR, sap);
        strcpy(sockcurrent->mask, msap);
        strcpy(sockcurrent->subnet, subnet);
        
        //bind(), bind the sockfd to the IP address: sa
        const int optval = 1;
        setsockopt(sockcurrent->sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
        bind(sockcurrent->sockfd, (SA *)&(sockcurrent->addr), sizeof(sockcurrent->addr));
        
        
        ifi = ifi->ifi_next;
        if(ifi != NULL){
            sockcurrent->nextsocket = malloc(sizeof(struct mysocks)); //malloc space for a new mysocks object;
            sockcurrent = sockcurrent->nextsocket;    //continue the linked list;
        }
    }
    
    printf("************************ the socketfd and IP info of client. **************************\n");
    for(sockcurrent = sockhead; sockcurrent != NULL; sockcurrent = sockcurrent->nextsocket){
        printf("=============================================\n");
        printf("socketfd: %d\n", sockcurrent->sockfd);
        printf("IP address: %s\n", sockcurrent->IPADDR);
        printf("mask: %s\n", sockcurrent->mask);
        printf("subnet: %s\n", sockcurrent->subnet);
    }
    printf("*********************************************************************************\n");
    
    
    
    //handle the sigchild, so there will be no zombies;
    signal(SIGCHLD, sig_chld);
    
    //****************************************************************************************************
    //****************************************************************************************************
    //here we begin to listen to the sockfds bond to the IP Addresses;
    firstclient = malloc(sizeof(struct client));
    currentclient = firstclient;
    clientnum = 0;
    int newcliflag = 1;
    
    //infinite loop for handle the clients
    //select(), select from mysocks
    
    
    printf("************************** start to listen to client ******************************\n");
    for(;;){
        FD_ZERO(&fds);
        
        //fork() returns pid;
        int pid;
        //FD_SET(), add sockfds to the fds struct;
        int maxfdp = 0;
        for(sockcurrent = sockhead; sockcurrent != NULL; sockcurrent = sockcurrent->nextsocket){
            
            //FD_SET(); put sockfds into fds;
            FD_SET(sockcurrent->sockfd, &fds);
            //get the maxfdp;
            if(maxfdp < sockcurrent->sockfd){
                maxfdp = sockcurrent->sockfd;
            }
        }
        
        //set maxfdp;
        maxfdp += 1;
        
        switch (select(maxfdp, &fds, NULL, NULL, NULL)) {
                
                
            case -1:
                printf("select error.\n");
                exit(0);
                break;
                
            default:
               
                for(sockcurrent = sockhead; sockcurrent != NULL; sockcurrent = sockcurrent->nextsocket){
                    if(FD_ISSET(sockcurrent->sockfd, &fds)){
                        //this socket has received a datagram, and we will fork a child to handle this client
                        
                        
                        int listenfd = sockcurrent->sockfd;
                        printf("the sockfd is: %d\n", listenfd);
                        char recvline[40];
                        int addrlen = sizeof(SA);
                        int recvnum;
                        struct msghdr msgrecv;
                        memset(&msgrecv, 0, sizeof(struct msghdr));
                        SA * source;
                        source = malloc(sizeof(SA));          //sockaddr_in IPclient;
                        struct iovec iovrecv[2];            //msg_iov;
                        char file_name[20];                 //file_name;
                        struct hdr *recvhdr;
                        recvhdr = malloc(sizeof(struct myHdr));
                        
                        
                        msgrecv.msg_name = source;         //
                        msgrecv.msg_namelen = addrlen;
                        msgrecv.msg_iov = iovrecv;
                        msgrecv.msg_iovlen = 2;
                        iovrecv[0].iov_base = (void *)&recvhdr;        //warning: incompatible pointer type;
                        iovrecv[0].iov_len = sizeof(struct myHdr);
                        iovrecv[1].iov_base = file_name;
                        iovrecv[1].iov_len = sizeof(file_name);
                        //int msg_flags = 0;
                        
                
                        recvnum = recvmsg(sockcurrent->sockfd, &msgrecv, 0);

                        if(recvnum < 0){
                            printf("recvmsg error!!!\n");
                            printf("error number: %d\n", errno);
                            exit(0);
                        }
                        
                        msgrecv.msg_iov[1].iov_base[recvnum - sizeof(struct myHdr)] = '\0';
                        printf("the file required: %s\n", msgrecv.msg_iov[1].iov_base);
                        strcpy(file_name, msgrecv.msg_iov[1].iov_base);
                        
                        
                        
                        //here we check if the IPclient is in the list of clients;
                        //=================================================================================
                        for(cursorclient = firstclient->nextclient; cursorclient != NULL; cursorclient = cursorclient->nextclient){
                            in_addr_t sourceIP = ((struct sockaddr_in *)source)->sin_addr.s_addr;
                            struct sockaddr_in * temp = (struct sockaddr_in *)&(cursorclient->IPclient);
                            in_addr_t storedIP = temp->sin_addr.s_addr;
                            
                            int sourceport = ((struct sockaddr_in *)source)->sin_port;
                            int storedport = temp->sin_port;
                            
                            
                            
                            
                            if(sourceIP == storedIP && sourceport == sourceport){
                                printf("there has been a client with the IP and portnum: %s\n", sock_ntop(source, sizeof(SA)));
                                newcliflag = 0;
                                break;
                            }
                            //else, it is a new client, go on to fork; 
                        }
                        //it is an old client, we don't fork child;
                        if(newcliflag == 0){
                            continue;
                        }
                        
                        
                        //create a pipe, use the pipe to send the pid of the child when it ends,
                        //and the parent will delete the corrosponding IPclient;
                        int pfd[2];
                        if(pipe(pfd) == -1){
                            printf("pipe failed.\n");
                            exit(0);
                        }
                        
                        //fork();
                        if((pid = fork()) < 0){
                            printf("fork error!\n");             //fork error;
                        }
                        //this is the child who handles the service from clients;
                        if(pid == 0){
                            //close other listening sockets;
                            for(sockcursor = sockhead; sockcursor != NULL; sockcursor = sockcursor->nextsocket){
                                if(sockcursor->sockfd != listenfd){
                                    close(sockcursor->sockfd);
                                }
                            }
                            
                            //here the child begin to handle the service
                            close(pfd[0]);
                            dup2(pfd[1], 11);
                            dg_filetransmission(listenfd, file_name, (SA *)&(sockcurrent->addr), (SA *)source);
                        }
                        
                        
                        currentclient->nextclient = malloc(sizeof(struct client));
                        currentclient = currentclient->nextclient;
                        currentclient->IPclient = *((SA *)msgrecv.msg_name);
                        currentclient->pid = pid;
                        clientnum ++;
                        printf("first client pid: %d\n", firstclient->nextclient->pid);
                        printf("now serving a new client with IP address: %s.\n", sock_ntop(&(currentclient->IPclient), sizeof(SA)));
                        printf("now the server is serving %d clients.\n", clientnum);
                        currentclient = currentclient->nextclient;

                        pthread_t tid;
                        close(pfd[1]);
                        dup2(pfd[0], 10);
                        int test = pthread_create(&tid, NULL, deleteIPclient, (void *)NULL);
                        pthread_detach(tid);
                    
                    }
                    
                }
                break;
        }
    
    }

}




