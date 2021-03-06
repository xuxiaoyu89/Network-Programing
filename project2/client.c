#include "unp.h"
#include "unpifiplus.h"
#include <math.h>

#define MAX 100

struct myHdr{
    uint32_t seq;
    uint32_t ts;
    
    int segmtNum;
    int adsize;
    int fin;
};


struct node{
    int segmt;
    struct myHdr recvhdr;
    char data[450];
    struct msghdr recvmsg;
    struct iovec iovrecv[2];
    struct node * next;
};

pthread_mutex_t linkList_mutex = PTHREAD_MUTEX_INITIALIZER; //mutex



static int sockfd;
static struct sockaddr_in newIPserver;
static struct node header;
static struct node * tail, *cursor;
static int windowsize;
static int currentseg;
static int currentsize;
static int currentadsize;
static int lastprinted;         //the segment number of which was printed out and deleted;
static int readfinflag;
static int printfinflag;
static int tailseg;
static int lastackedseg;


static int seed_value;
static double p;
static int u;


float myrandom(int seed){
    srand(seed);
    float x = rand()%100;
    float y = x/100;
    return y;
}



/************************** sendack() *******************************/
void sendack(int segmt, int adsize){
    struct msghdr ACK;
    struct myHdr ackhdr;
    struct iovec ackvec[1];
    
    memset(&ACK, 0, sizeof(struct msghdr));
    memset(&ackhdr, 0, sizeof(struct myHdr));
    
    ackhdr.segmtNum = segmt;
    ackhdr.adsize = adsize;
    
    ACK.msg_name = NULL;
    ACK.msg_namelen = 0;
    ACK.msg_iov = ackvec;
    ACK.msg_iovlen = 1;
    ackvec[0].iov_base = (char *)&ackhdr;
    ackvec[0].iov_len = sizeof(struct myHdr);
    //printf("from sendack(), segment#: %d!!!!!!!!!!!!!!!!!!!!\n", ackhdr.segmtNum);
    
    float randomnum = rand()/(RAND_MAX + 1.0);
    if(randomnum < p){
        printf("ack of segment %d dumped!\n", segmt);
        return; 
    }
    
    int sendnum = sendmsg(sockfd, &ACK, 0);
    if(sendnum < 0){
        printf("send ACK error, errno is: %d\n", errno);
        exit(0);
    }
}


/************************************ this is the thread function to print out file data *************************************/
static void * printseg(){
    //pthread_detach( pthread_self() );
    printfinflag = 0;
    
    while(printfinflag == 0){
        pthread_mutex_lock(&linkList_mutex);
        printf("\n****************************** print thread ******************************\n");
        struct node *temp;
        temp = header.next;
        //printf("============================= print thread starts ==========================\n");
        //print all the available data and delete the node;
        /*after print and delete, we should send an ACK to server
          in the ACK, there should be these information:
          1)the new window size;
          2)the last segment we print(tell the server which segment to send);
         */
        while( temp != NULL ){
            /*
              before print the data, we have to check it first;
    　　　     there are two situations:
        　     1)the node has no data at all, means that the segment has not been received yet;
        　     2)the fin in the node is 1, means that it is the last segment.
                 At this situation, we should
            */
            //invalid data;
            if(strlen(temp->data) == 0){
                break;
            }
            //EOF
            if((temp->recvhdr).fin == 1){
                printfinflag = 1;
            }
        
            /*else, this is a node with valid data, then we will do these things:
                1)print out the data,
                2)delete the node,
                3)currentadsize ++;
            */
            printf("\n============================== segment %d ===============================\n", temp->segmt);
            printf("%s\n\n", temp->data);
            lastprinted = temp->segmt;
            header.next = temp->next;
            free(temp);
            temp = header.next;
            currentadsize ++;
            currentsize --;
            
            if(temp == NULL){
                tail = &header;
            }
        }
        /*after we print the data out and delete the nodes, we should
          send an ACK to server to tell the updated window and expected segment;
         */
        if(printfinflag == 0){
            if(lastprinted != 0){
                sendack(lastprinted, currentadsize);
            }
        }
        pthread_mutex_unlock(&linkList_mutex);
        //printf("\n===========================printf thread ends ============================\n");
        
        int sleepTime = (-1) * u * log(myrandom(seed_value));
        sleep(sleepTime/1000+1);
    }
    printf("all segments are printed out, print thread ends!\n");
    pthread_exit(NULL);
    return (NULL);
}


/************************************ this is the main thread function to recv data from server ************************************/
//there are two threads: one, which is the main thread, to read file from the udp socket and put the segment into the receive window;
//                       the other consumes the segments and prints them out;

/*basically, this receive process enters a for loop:
 for{
 receive a segment, check if it is the port number(resend...).
 if it is the port number, we send back an "ack";
 otherwise, we put the segment into the receive window and send back a ACK massage;
 }
 */
void recv_recv(struct sockaddr_in newIPserver){
    
    readfinflag = 0;
    //create a new thread: this thread consumes the segments and print them out;
    pthread_t tid;
    pthread_create(&tid, NULL, printseg, NULL);
    
    memset(&header, 0, sizeof(struct node));
    header.segmt = 0;
    tail = &header;
    tailseg = 0;
    currentadsize = windowsize;
    while(readfinflag == 0){

   
        /******* mutex lock *******/
        //pthread_mutex_lock(&linkList_mutex);

        
        sleep(0.1);
        
        /******* mutex lock *******/
        pthread_mutex_lock(&linkList_mutex);
        //printf("main thread get mutex!!!!!!!!!!\n");
        if(currentsize >= windowsize){
            pthread_mutex_unlock(&linkList_mutex);
            continue;
        }
        struct msghdr tempmsg;
        struct myHdr temphdr;
        struct iovec tempvec[2];
        char tempdata[450];
        
        memset(&tempmsg, 0, sizeof(struct msghdr));
        memset(&temphdr, 0, sizeof(struct myHdr));
        
        tempmsg.msg_name = NULL;
        tempmsg.msg_namelen = 0;
        tempmsg.msg_iov = tempvec;
        tempmsg.msg_iovlen = 2;
        
        tempvec[0].iov_base = (char *)&temphdr;
        tempvec[0].iov_len = sizeof(struct myHdr);
        tempvec[1].iov_base = tempdata;
        tempvec[1].iov_len = 450;
        
        //printf("before recvmsg()!!!!!!!!!!!!!!!!!!\n");
        int recvnum = recvmsg(sockfd, &tempmsg, 0);
                if(recvnum < 0){
            printf("recvmsg error!!!\n");
            printf("error number: %d\n", errno);
            exit(0);
        }
        //printf("after recvmsg()!!!!!!!!!!!!!!!\n");
        /*
         Here we will do something to simulate the poor connection situation of the internet:
         when we receive a segment from the server, we dump it in a certain probability(p);
         */
        //ignore the message;
        
        float randomnum = rand()/(RAND_MAX + 1.0);
        if(randomnum < p){
            printf("segment %d dumped!\n", temphdr.segmtNum);
            /******* mutex Unlock *******/
            pthread_mutex_unlock(&linkList_mutex);
            continue;
        }
        
        //if the segment in the message is the last segment;
        if(temphdr.fin == 1){
            readfinflag = 1;
        }
        
        
        currentseg = temphdr.segmtNum;
        printf("the currentseg: %d!!!!!!!!!!!!!!!!!\n", currentseg);
        
        //if the message carries the port number;
        //*************************************************
        if(currentseg == 0){
            printf("this is the resent port number.\n");
            
            char ACK[3];
            strcpy(ACK, "ack");
            int sendnum;
            //sendnum = sendto(sockfd, ACK, strlen(ACK), 0, (SA *)&newIPserver, servlen);
            //here we use sendmsg;
            //define a msghdr struct;
            struct msghdr msgsend_ack;
            memset(&msgsend_ack, 0, sizeof(msgsend_ack));
            struct myHdr ackhdr;
            struct iovec iovsend[2];
            
            ackhdr.seq = 0;
            msgsend_ack.msg_name = NULL;
            msgsend_ack.msg_namelen = 0;
            msgsend_ack.msg_iov = iovsend;
            msgsend_ack.msg_iovlen = 2;
            iovsend[0].iov_base = (char *)&ackhdr;
            iovsend[0].iov_len = sizeof(struct myHdr);
            iovsend[1].iov_base = ACK;
            iovsend[1].iov_len = sizeof(ACK);
            
            sendnum = sendmsg(sockfd, &msgsend_ack, 0);
            
            printf("sendnum is: %d\n", sendnum);
            if(sendnum < 0){
                printf("ack error: %d\n", errno);
            }
            pthread_mutex_unlock(&linkList_mutex);
            continue;
        }

        
        if(currentseg == tailseg){
            printf("there is already data!!!!!!!!!!!\n");
            sendack(lastackedseg, currentadsize);
            pthread_mutex_unlock(&linkList_mutex);
            continue;
        }
        else if(currentseg < tailseg){            //we get something lost before(or we have get it, but server did not get ACK);
            cursor = header.next;
            while(cursor->segmt != currentseg){
                cursor = cursor->next;
            }
            int datasize = strlen(cursor->data);
            if(datasize == 0){                    //there is no data, this segment has not been received yet;
                strcpy(cursor->data, tempdata);   //copy the data;
                /*
                 if it is the last segment, set fin = 1,
                 so in the print thread, after print the last segment, 
                 it will not send ack(to tell server adwindowsize),
                 because now the server has stopped to receive message from client;
                 */
                if(readfinflag == 1){
                    (cursor->recvhdr).fin = 1;
                }
                lastackedseg = currentseg;
            }
            else{                                 //there is data, we have get it before, but the ACK was somehow missing;
                printf("there is already data!!!!!!!!!!!\n");
                sendack(lastackedseg, currentadsize);
                pthread_mutex_unlock(&linkList_mutex);
                continue;
            }
            
        }
        else{                                     //we get segment after tail(maybe right after, or several nodes after)
            
            if(currentseg - tailseg + currentsize > windowsize){
                printf("no more spaces!!!!\n");
                sendack(lastackedseg, currentadsize);
                pthread_mutex_unlock(&linkList_mutex);
                continue;
            }
            //printf("tailseg: %d!!!!!!!!!!!!!!\n", tailseg);
            cursor = tail;
            while(tailseg < currentseg){
                cursor->next = malloc(sizeof(struct node));
                cursor->next->segmt = tailseg + 1;
                //printf("currentsize before: %d\n", currentsize);
                currentsize = currentsize + 1;
                //printf("currentsize after: %d\n", currentsize);
                currentadsize = currentadsize -1;
                cursor = cursor->next;
                tailseg ++;
                if(cursor->segmt == currentseg){
                    strcpy(cursor->data, tempdata);   //copy the data;
                    if(readfinflag == 1){
                        (cursor->recvhdr).fin = 1;
                    }
                    lastackedseg = currentseg;
                }
            }
            tail = cursor;
        }
        //here we update the lastackedseg;
        tail->next = NULL;
        cursor = header.next;
        if(strlen(cursor->data) == 0){
            lastackedseg = lastprinted;
        }
        else{
            while(strlen(cursor->data) != 0){
                lastackedseg = cursor->segmt;
                if(cursor->next == NULL){
                    break;
                }
                cursor = cursor->next;
            }
        }
        
        //******** send back ACK to the server ************;
        //in the ACK, we should tell the server: 1)segment number, 2)the adwindow size;
        //should there be the sequence number and the time stamp??????????????????????
        sendack(lastackedseg, currentadsize);
        
        printf("recvd segment#: %d| current window size: %d| adwindow: %d| fin: %d| ",  currentseg, currentsize, currentadsize, (tail->recvhdr).fin);
        printf("ACKed #: %d\n", lastackedseg);
        
        if((tail->recvhdr).fin == 1){
            printf("get the EOF, now stop receiving.");
            readfinflag = 1;
        }
    
        /******* mutex Unlock *******/
        pthread_mutex_unlock(&linkList_mutex);
    }
    printf("all the segments are received, now stop receiving!\n");
    //wait for the child thread;
    printf("waiting for the printing thread to finish...\n");
    pthread_join(tid, NULL);
}


void mydg_cli(char *file_name, const SA *IPserver, int servlen){
    int recvnum;               //received bytes in the recvmsg function;
    char servport[10];         //received ephemeral port number of server.
    printf("file_name length: %d\n", strlen(file_name));
    file_name[strlen(file_name)] = '\0';
    dg_send_recv(sockfd, 0, (void *)file_name, strlen(file_name), (void *)servport, sizeof(servport), IPserver, servlen, 1, p);
    servport[5] = '\0';
    
    printf("the servport is: %s\n", servport);
    int servport_int = atoi(servport);
    //printf("servport_int: %d\n", servport_int);
    
    //((struct sockaddr_in *)IPserver)->sin_port = htons(servport_int);
    

    
    //create a new IPserver;
    memset(&newIPserver, 0, sizeof(struct msghdr));
    newIPserver.sin_family = AF_INET;
    newIPserver.sin_port = htons(servport_int);
    newIPserver.sin_addr.s_addr = ((struct sockaddr_in *)IPserver)->sin_addr.s_addr;
    printf("the new IPserver IP address: %s\n", sock_ntop((SA *)&newIPserver, sizeof(SA)) );
    
    //client reconnect to server with the new ephemaral port number;
    
    int conn = connect(sockfd, (SA *)&newIPserver, servlen);
    if(conn < 0){
        printf("reconnect error!\n");
        exit(0);
    }
    
    
    //getpeername()
    struct sockaddr_in sa;
    int len_server = sizeof(SA);
    if (!getpeername(sockfd, (struct sockaddr *)&sa, &len_server))
    {
        printf("\n------------------------- getpeername() -------------------------\n ");
        printf( "IP of Server: %s\n ", inet_ntoa(sa.sin_addr));
        printf( "port number of Server: %d\n ", ntohs(sa.sin_port));
    }

    
    
    char ACK[3];
    strcpy(ACK, "ack");
    int sendnum;
    //sendnum = sendto(sockfd, ACK, strlen(ACK), 0, (SA *)&newIPserver, servlen);
    //here we use sendmsg;
    //define a msghdr struct;
    struct msghdr msgsend_ack;
    memset(&msgsend_ack, 0, sizeof(msgsend_ack));
    struct myHdr ackhdr;
    struct iovec iovsend[2];
    
    ackhdr.seq = 0;
    msgsend_ack.msg_name = NULL;
    msgsend_ack.msg_namelen = 0;
    msgsend_ack.msg_iov = iovsend;
    msgsend_ack.msg_iovlen = 2;
    iovsend[0].iov_base = (char *)&ackhdr;
    iovsend[0].iov_len = sizeof(struct myHdr);
    iovsend[1].iov_base = ACK;
    iovsend[1].iov_len = sizeof(ACK);
    
    sendnum = sendmsg(sockfd, &msgsend_ack, 0);
    
    printf("sendnum is: %d\n", sendnum);
    if(sendnum < 0){
        printf("ack error: %d\n", errno);
    }
    
    /******************************Here we begin to receive the data of file_name**********************************/
    
    printf("\n====================== initialize ended =========================\n");
    printf("\n====================== start to file transfer =========================\n");
    recv_recv(newIPserver);
    printf("got the whole file, client exits.\n");
    
}


int main(int argc, char **argv)
{
    FILE *fp;                   //指向client.in文件的指针
    
    //字符数组接收client.in中的参数
    char argument[7][20];
    //根据变量类型接收client.in中的参数
    struct sockaddr_in servaddr;
    int port;
    char file_name[MAX];
    int sliding_window_size;
    
    fp=fopen("client.in","r");
    
    char msg[20];               //用于临时储存从client.in文件中读出的参数
    int i=0;                    //循环控制变量i
    
    for(i=0;i<7;i++)
    {
        memset(msg,0,sizeof(msg));
        fgets(msg,sizeof(msg)-1,fp);
        strcpy(argument[i],msg);
    }
    
    printf("\n========================== initialize the client =============================\n");
    
    //显示读入的数据
    /*
    for(i=0;i<7;i++)
    {
        printf("%s", argument[i]);      //注意：argument[i]自带换行符，除了最后一个，因为在client.in文件中最后一行没有换行
    }
     */
    printf("\n======================== arguments from client.in ============================\n");
    //读入数据转换成他们实际应该是的变量类型
    port=atoi(argument[1]); //端口号
    printf("\nport = %d\n",port);
    
    bzero(&servaddr, sizeof(servaddr));//IP地址
    servaddr.sin_family=AF_INET;
    servaddr.sin_port=port;
    argument[0][strlen(argument[0])-1]=0;//IP地址去掉换行符
    if ( inet_pton(AF_INET, argument[0], &servaddr.sin_addr)<=0 )
    {
        fputs("inet_pton error\n", stdout);
        exit(1);
    }

    
    strcpy(file_name,argument[2]);          //filename
    file_name[strlen(file_name)-1]=0;
    printf("%s\n", file_name);
    
    sliding_window_size=atoi(argument[3]);  //sliding_window_size
    windowsize = sliding_window_size;
    printf("sliding_window_size = %d\n", sliding_window_size);
    
    seed_value=atoi(argument[4]);           //seed_value
    printf("seed_value = %d\n", seed_value);
    //seed_value = time(NULL);
    
    p=atof(argument[5]);
    printf("p = %f\n", p);                  //p
    
    u=atoi(argument[6]);
    printf("u = %d\n", u);                  //u
    printf("============= the IP addresses and masks of the client =============\n");
    
    
    //显示所有IP地址和子网掩码
    struct ifi_info *ifi, *ifihead;
    struct sockaddr_in *sa, *msa;
    int j=1;
    for(ifihead=ifi = get_ifi_info_plus(AF_INET, 1); ifi != NULL; ifi = ifi->ifi_next, j++)
    {
        sa = (struct sockaddr_in *)ifi->ifi_addr;
        msa = (struct sockaddr_in *)ifi->ifi_ntmaddr;
        printf("the %dth IP address: %s\n", j, sock_ntop((SA *)sa, sizeof(*sa)));
        printf("Mask address: %s\n", sock_ntop((SA *)msa, sizeof(*msa)));
    }
//    printf("j = %d\n", j);  //j=4 here
    
    printf("================================================================\n");
    printf("the IPserver is: %s", argument[0]);
    
    //检查client的IP和从client.in读近来的服务器IP是否在同一个主机(host)或者子网(subnet)上，确定IPclient和IPserver
    int h=0;    //检查loacal的for循环的循环次数
    int k=0;    //检查same host的for循环次数
    char loopback_ip[40]="127.0.0.1";
    struct sockaddr_in *sacli_host, *msacli_host, *subaddr, IPclient, IPserver;
    struct sockaddr_in *sacli_local, *msacli_local;
    
    
    
    //检查是否是同一主机上(check if same host)
    for(ifihead = ifi = get_ifi_info_plus(AF_INET, 1); ifi != NULL; ifi = ifi->ifi_next)
    {
        sacli_host = (struct sockaddr_in *)ifi->ifi_addr;
        
        if ( (*sacli_host).sin_addr.s_addr == servaddr.sin_addr.s_addr )
        {
            printf("the client and server are in the same host\n");
            
            //same host情况下，对IPserver初始化
            bzero(&IPserver, sizeof(IPserver));
            IPserver.sin_family = AF_INET;
            IPserver.sin_port = htons(port);
            if ( inet_pton(AF_INET, loopback_ip, &IPserver.sin_addr)<=0 )
            {
                fputs("under same host situation, inet_pton error for\n",stdout);
                exit(1);
            }
            //same host情况下，对IPclient初始化
            IPclient = (*sacli_host);
            
            break;
        }
        k++;
    }
    
    
    //检查是否再同一子网里(chenck if same subnet)
    if ( k==3 )
    {
        for(ifihead = ifi = get_ifi_info_plus(AF_INET, 1); ifi != NULL; ifi = ifi->ifi_next)
        {
            sacli_local = (struct sockaddr_in *)ifi->ifi_addr;
            msacli_local = (struct sockaddr_in *)ifi->ifi_ntmaddr;
                
            if ( (((*sacli_local).sin_addr.s_addr) & ((*msacli_local).sin_addr.s_addr)) == ((servaddr.sin_addr.s_addr) & ((*msacli_local).sin_addr.s_addr)) )
            {
                printf("\nlocal host\n");
            
                //loca情况下，对IPserver初始化
                IPserver = servaddr;
                //local情况下，对IPclient初始化
                IPclient = (*sacli_local);
                
                break;
            }
            h++;
            //        printf("IP: %d \n", (*sacli).sin_addr.s_addr);
            //        printf("mask: %d \n", (*msacli).sin_addr.s_addr);
            //        printf("result of $ between mask and client IP: %d \n", ((*sacli).sin_addr.s_addr) & ((*msacli).sin_addr.s_addr));
        }
    }
    //非local情况下，对IPserver 和 IPclient初始化
//    printf("h = %d\n", h);
    if ( h==j-1 )       //说明是循环结束时候还没有发现是same host或者same local的
    {
        printf("\nNOT local or same host !!!\n");
        IPserver = servaddr;
        IPclient = (*sacli_local);
    }
    
    //输出IPclient 和 IPserver
    printf("====================================================\n");
    printf("IPclient: %s\n", sock_ntop((SA*)&IPclient, sizeof(IPclient)));
    printf("IPserver: %s\n", sock_ntop((SA*)&IPserver, sizeof(IPserver)));
    printf("====================================================\n");
    
    
    /**********IPserver和IPclient已经确定好了，但是IPclient端口号此时为空******************/
    
    //创建socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    
    /*IPclient bind开始，port=0*/
   // bzero(&IPclient, sizeof(IPclient));
    IPclient.sin_family = AF_INET;
    IPclient.sin_port = htons(0);
    bind(sockfd, (SA *) &IPclient, sizeof(IPclient));
    /*IPclident bind结束*/
    
    //getsockname 部分
    struct sockaddr_in p_IPclient;
    socklen_t len_IPclient;
    len_IPclient = sizeof(IPclient);
    getsockname(sockfd, (SA *) &p_IPclient, &len_IPclient);
    printf("\n------------------------- getsockname() -------------------------\n ");
    printf("IPclient with ephemeral port number: %s\n", sock_ntop((SA*)&p_IPclient, sizeof(p_IPclient)));

    
    mydg_cli(file_name, (SA *)&IPserver, sizeof(IPserver));

    printf("\n===================program ends=========================\n");
    
}







