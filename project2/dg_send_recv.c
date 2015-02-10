#include "unprtt_plus.h"
#include <setjmp.h>

#define RTT_DEBUG        //??????????????????????????????????????????????

static struct rtt_info rttinfo;
static int rttinit = 0;
static struct msghdr msgsend, msgrecv;
static struct hdr{
    uint32_t seq;         //sequence
    uint32_t ts;           //time stamp
    int segmtNum;
    int adSize;
    int fin;

    
} sendhdr, recvhdr;

static void sig_alrm(int signo);
static sigjmp_buf jmpbuf;

/*
 int this function we add two more arguments to the original dg_send_recv function: int connfd and int flag;
 int connfd: when retransmit the port number, we will use both listenfd and connfd;
 int flag: 1 if this function is used to transfer file_name,
             in which situation we don't have to check the sequence number, and have only one socket fd;
             so the sequence number is set to 0;
           0 if this function is used to transfer data in file;
*/

ssize_t dg_send_recv(int fd1, int fd2, void *outbuff, size_t outbytes, void *inbuff, size_t inbytes, SA * destaddr, socklen_t destlen, int flag, int p){
    ssize_t n;
    struct iovec iovsend[2], iovrecv[2];
    
    if(rttinit == 0){
        rtt_init(&rttinfo);
        rttinit = 1;
    }
    
    if(flag > 0){
        sendhdr.seq = 0;
    }
    else{
        sendhdr.seq++;
    }
    msgsend.msg_name = destaddr;
    msgsend.msg_namelen = destlen;
    msgsend.msg_iov = iovsend;
    msgsend.msg_iovlen = 2;
    iovsend[0].iov_base = (char *)&sendhdr;
    iovsend[0].iov_len = sizeof(struct hdr);
    iovsend[1].iov_base = outbuff;
    iovsend[1].iov_len = outbytes;
    
    
    msgrecv.msg_name = NULL;
    msgrecv.msg_namelen = 0;
    msgrecv.msg_iov = iovrecv;
    msgrecv.msg_iovlen = 2;
    iovrecv[0].iov_base = (char *)&recvhdr;
    iovrecv[0].iov_len = sizeof(struct hdr);
    iovrecv[1].iov_base = inbuff;
    iovrecv[1].iov_len = inbytes;
    
    signal(SIGALRM, sig_alrm);
    rtt_newpack(&rttinfo);
    
sendagain:
    sendhdr.ts = rtt_ts(&rttinfo);
    sendmsg(fd1, &msgsend, 0);
    if(flag == 2){
        float randomnum = rand()/(RAND_MAX + 1.0);
        if(randomnum > p){
            sendmsg(fd2, &msgsend, 0);
        }
        else{
            printf("the client dumped the filename!!!!!\n");
        }
    }
    alarm(rtt_start(&rttinfo)/1000000);
    
    if(sigsetjmp(jmpbuf, 1) != 0){
        if(rtt_timeout(&rttinfo) < 0){
            printf("dg_send_recv: no response from server, giving up");
            rttinit = 0;
            errno = ETIMEDOUT;
            return (-1);
        }
        goto sendagain;
    }
    
    do{
        if(flag == 1) {
            n = recvmsg(fd1, &msgrecv, 0);
            float randomnum = rand()/(RAND_MAX + 1.0);
            if(randomnum < p){
                printf("client dumped the ack from server!!!!!!!!!!!\n");
                continue;
            }
        }
        ((char *)inbuff)[n - sizeof(struct hdr)] = '\0';
    }while(recvhdr.seq != sendhdr.seq);
    
    alarm(0);
    rtt_stop(&rttinfo, rtt_ts(&rttinfo) - recvhdr.ts);
    
    return (n - sizeof(struct hdr));
}

static void sig_alrm(int signo){
    siglongjmp(jmpbuf, 1);
}




















