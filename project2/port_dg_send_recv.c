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
 int this function we add one more arguments to the original dg_send_recv function: int fd2
 int fd2: when retransmit the port number, we will use both listenfd and connfd;
*/

ssize_t port_dg_send_recv(int fd1, int fd2, void *outbuff, size_t outbytes, void *inbuff, size_t inbytes, SA * destaddr, socklen_t destlen){
    ssize_t n;
    struct iovec iovsend[2], iovrecv[2];
    struct fd_set fds;
    
    if(rttinit == 0){
        rtt_init(&rttinfo);
        rttinit = 1;
    }
    
    sendhdr.seq = 0;
    sendhdr.segmtNum = 0;     //here we tell the client: this massage is the port number, if segmtNum > 0, it is data in the file;

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
        //select;
        FD_ZERO(&fds);
        FD_SET(fd1, &fds);
        FD_SET(fd2, &fds);
        
        int maxfdp = fd1>fd2?fd1+1:fd2+1;
        switch(select(maxfdp, &fds, NULL, NULL, NULL)){
            case -1:
                printf("select error!\n");
                exit(0);
            
            default:
                //listenfd: we get another file_name, so we have to send portnum again;
                if(FD_ISSET(fd1, &fds)){
                    goto sendagain;
                }
                //connfd: we get the ack from client, and 
                if(FD_ISSET(fd2, &fds)){
                    n = recvmsg(fd2, &msgrecv, 0);
                    ((char *)inbuff)[n - sizeof(struct hdr)] = '\0';
                }
        }
                
    }while(recvhdr.seq != sendhdr.seq);
    alarm(0);
    rtt_stop(&rttinfo, rtt_ts(&rttinfo) - recvhdr.ts);
    return (n - sizeof(struct hdr));
    
}
    
static void sig_alrm(int signo){
    siglongjmp(jmpbuf, 1);
}




















