#include "unp.h"
#include "hw_addrs.h"
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/in_systm.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>

#include <setjmp.h>                  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <time.h>

#define VM_NUM 20
#define ROUTE_LEN 256

#define MULTICAST_IP "239.255.2.31"
#define MULTICAST_PORT "2880"

#define IPPROTO_TOUR 146
#define IDENTIFICATION_TOUR 2211
#define IDENTIFICATION_PING 3311

struct hwaddr{
    int sll_ifindex;              //interface number;
    unsigned short sll_hatype;    //hardware type;
    unsigned char sll_halen;      //length of address;
    unsigned char sll_addr[6];       //physical layer address;
};

struct pf_ip_icmp {
	unsigned char dst_mac[ETH_ALEN];
	unsigned char src_mac[ETH_ALEN];
	uint16_t type;
	char buf[84];
};


struct rt_datagram{
    // ip header
    struct ip header;
    // data
    struct in_addr nodes[ROUTE_LEN];
    struct in_addr host;
    int pptr;                       //position of next hop
    int len;                        //lenth of nodes
    char mac_ip[INET_ADDRSTRLEN];    //multicast IP
	int mac_port;                    //multicast port number
};



struct msg{
    int check_flag; //if the msg is 'tour ends' msg, check_flag=1, else check_flag=0
    char buff[MAXLINE];
};



struct in_addr my_addr;

int mc_recvnum, mc_sendnum;
socklen_t salen;
int sock_mc_recv, sock_mc_send;
struct sockaddr *sasend, *sarecv;
const int on = 1;


struct msg recv_msg, send_msg;


char vmx[4];
struct hostent *h;

char vml[4];    //last node name, for ping()


int rt_sock;
int pg_sock, pf_sock;

//for ping() call
int my_if_index;
unsigned char my_if_hwaddr[6];
int seq = 0;
int pid;

//for time use
char timebuf[MAXLINE];
time_t ticks;

static sigjmp_buf jmpbuf;        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

static void sig_alrm(int signo){
    siglongjmp(jmpbuf, 1);
}


//==============
//timeout!!!!!!!!!!!!
/************************ areq() *************************/
int areq(struct sockaddr * IPaddr, socklen_t sockaddrlen, struct hwaddr * HWaddr){
    int sockfd;
    struct sockaddr_un addr, arpaddr;
    int IP;
    int test;
    int i;
    char * ptr;
    
    char buff[MAXLINE];
    
    signal(SIGALRM, sig_alrm);          //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    
    unlink("tour_cse533-12");
    
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, "tour_cse533-12");
    
    memset(&arpaddr, 0, sizeof(arpaddr));
    arpaddr.sun_family = AF_UNIX;
    strcpy(arpaddr.sun_path, "arp_cse533-12");
    
    test = bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    if(test < 0){
        printf("bind error, errno: %d\n", errno);
        exit(0);
    }
    
    //connect
    test = connect(sockfd, (struct sockaddr *)&arpaddr, sizeof(arpaddr));
    if(test < 0){
        printf("connect error, errno: %d\n", errno);
        exit(0);
    }
    
    //printf("connected\n");
    
    IP = (((struct sockaddr_in *)IPaddr)->sin_addr).s_addr;
    memcpy((void *)buff, (void *)&IP, 4);
    //send;
    test = send(sockfd, buff, strlen(buff), 0);
    if(test < 0){
        printf("send error, errno: %d\n", errno);
        exit(0);
    }
    
    //print info;
    
    
    //printf("after send\n");
    alarm(5);                           //~~~~~~~~~~~~~~~~~~~~~~~~~~~
    if(sigsetjmp(jmpbuf, 1) != 0){
        //timeout, close the connection socket, return 0;
        
        printf("timeout!!!!!!!!!!!!!!!!!!!!!\n");
        close(sockfd);
        return 0;
    }
    
    
    //recv;
    
    //printf("before recv\n");
    memset(buff, 0, sizeof(buff));
    test = recv(sockfd, buff, sizeof(buff), 0);
    if(test < 0){
        printf("recv error, errno: %d\n", errno);
        exit(0);
    }
    //printf("after recv\n");
    //recvd, cancel the alarm;
    alarm(0);                           //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    memcpy(HWaddr->sll_addr, buff, 6);
    
    ptr = buff;
    i = IF_HADDR;
    printf("MAC of previous node:");
    do{
        printf("%.2x%s", *ptr++ & 0xff, (i == 1) ? "\n\n" : ":");
    }while(--i > 0);
    
    return 1;
    
}




///////////////////////////////////////////////////////////////////
uint16_t
in_cksum(uint16_t *addr, int len)
{
	int				nleft = len;
	uint32_t		sum = 0;
	uint16_t		*w = addr;
	uint16_t		answer = 0;
    
	/*
	 * Our algorithm is simple, using a 32 bit accumulator (sum), we add
	 * sequential 16 bit words to it, and at the end, fold back all the
	 * carry bits from the top 16 bits into the lower 16 bits.
	 */
	while (nleft > 1){
		sum += *w++;
		nleft -= 2;
	}
    
	/* 4mop up an odd byte, if necessary */
	if (nleft == 1){
		*(unsigned char *)(&answer) = *(unsigned char *)w ;
		sum += answer;
	}
    
	/* 4add back carry outs from top 16 bits to low 16 bits */
	sum = (sum >> 16) + (sum & 0xffff);	/* add hi 16 to low 16 */
	sum += (sum >> 16);			/* add carry */
	answer = ~sum;				/* truncate to 16 bits */
	return (answer);
}

///////////////////////////////////////////////////////////////////



/*************************** ping() ******************************/
void ping(int prevIP, struct rt_datagram packet){
    struct sockaddr_in prevADDR;
    struct hwaddr HWaddr;
    
    int test;
    
    struct sockaddr_ll pf_addr, pre_addr;
    int i;
    char * charptr;
    
    
    
    //////这个结构搞不懂唉，大腿这么写就这么写吧///////
    struct pf_ip_icmp pa;
    struct ethhdr *eh;
    struct ip *hdr;
    struct icmp *ptr;
    
 //   struct sockaddr_in dest_addr;
    ////////////////////////////////////////////
    
    
    
    memset(&HWaddr, 0, sizeof(HWaddr));
    prevADDR.sin_addr.s_addr = prevIP;
    
    printf("Call areq()\n");
    
    
    test = areq((struct sockaddr *)&prevADDR, sizeof(prevADDR), &HWaddr);
    
    //test = 1, send ping message
    
    if(test == 0){
        exit(0);
    }
    
    printf("returned MAC addr from areq(): ");
    charptr = HWaddr.sll_addr;
    i = IF_HADDR;
    do{
        printf("%.2x%s", *charptr++ & 0xff, (i == 1) ? "\n" : ":");
    }while(--i > 0);
    
    
    //'pf' PF_PACKET raw socket receives ping reply
    if ((pf_sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP))) < 0)
    {
        printf("pg_sock error!\n");
        exit(1);
    }
    
    //init pf_addr, this is the sender
    bzero(&pf_addr, sizeof(pf_addr));
    pf_addr.sll_family = PF_PACKET;
    pf_addr.sll_protocol = htons(ETH_P_IP);
    pf_addr.sll_ifindex = my_if_index;
    pf_addr.sll_hatype = ARPHRD_ETHER;
	pf_addr.sll_pkttype = PACKET_HOST;
	pf_addr.sll_halen = ETH_ALEN;
    for (i = 0; i < 6; i++){
		pf_addr.sll_addr[i] = my_if_hwaddr[i];
	}
    //pf_addr.sll_addr[6] = 0x00;
    //pf_addr.sll_addr[7] = 0x00;
	if(bind(pf_sock, (SA *)&pf_addr, sizeof(pf_addr)) < 0){
        printf("ping bind error, %d\n", errno);
        exit(0);
    }
    
    //this is the receiver
    bzero(&pre_addr, sizeof(pre_addr));
    pre_addr.sll_family = PF_PACKET;
    pre_addr.sll_protocol = htons(ETH_P_IP);
    pre_addr.sll_ifindex = my_if_index;
    pre_addr.sll_hatype = ARPHRD_ETHER;
	pre_addr.sll_pkttype = PACKET_HOST;
	pre_addr.sll_halen = ETH_ALEN;
    for (i = 0; i < 6; i++){
		pre_addr.sll_addr[i] = HWaddr.sll_addr[i];
	}
    //pre_addr.sll_addr[6] = 0x00;
    //pre_addr.sll_addr[7] = 0x00;
    
    
    while(1)
    {
        ////////////////搞不懂的结构就是这个唉，站在icmp顶端的男人/////////////////
        //construct PF_PACKET SOCK_RAW type icmp
        //construct ethernet frame header
        eh = (struct ethhdr *)&pa;
        for (i=0; i<6; i++)
        {
            pa.dst_mac[i] = HWaddr.sll_addr[i];
        }
        for(i=0; i<6; i++)
        {
            pa.src_mac[i] = my_if_hwaddr[i];
        }
        eh->h_proto = htons(ETH_P_IP);
        //construct ip datagram header
        hdr = (struct ip *)pa.buf;
        hdr->ip_v = 4;
        hdr->ip_hl = 5;
        hdr->ip_tos = 0;
        hdr->ip_len = htons(84);
        hdr->ip_id = htons(0);
        hdr->ip_off = 0x0;
        hdr->ip_ttl = 64;
        hdr->ip_p = IPPROTO_ICMP;
        hdr->ip_src = my_addr;
        hdr->ip_dst = packet.host;
    
        
        hdr->ip_sum = 0;
        hdr->ip_sum = in_cksum((u_short *)hdr, 20);
        //construct icmp data
        ptr = (struct icmp *)(pa.buf + 20);
        ptr->icmp_type = ICMP_ECHO;
        ptr->icmp_code = 0;
        
        pid = getpid() & 0xffff;
        
        printf("child pid: %d\n", pid);
        ptr->icmp_id = pid;
        ptr->icmp_seq = seq;
        memset(ptr->icmp_data, 0xa5, 56);
        Gettimeofday((struct timeval *)ptr->icmp_data, NULL);
        ptr->icmp_cksum = 0;
        ptr->icmp_cksum = in_cksum((u_short *)ptr, 64);
    
        /////////////////////////////////////////////////////////////////////
        
        h = gethostbyaddr((char *)&packet.host, sizeof(struct in_addr), AF_INET);
        strcpy(vml, h->h_name);
        vml[4] = 0; //null terminate
        
        seq++;
        printf("PING %s (%s): %d data bytes\n", vml, inet_ntoa(packet.host), 64);
        if (sendto(pf_sock, &pa, sizeof(pa), 0, (SA *)&pre_addr, sizeof(pre_addr)) < 0 )
        {
            printf("ping send to error, %d\n", errno);
            exit(1);
        }
        memset(&pa, 0, sizeof(pa));
        sleep(1);
    }
    exit(0);
}


/************************** recv_ping() **********************************/
void recv_ping()
{
    int hlen1, icmplen;
    double rtt;
	struct ip *ip;
	struct icmp *icmp;
    struct timeval *tvsend;
    int r;
    
    struct ip *hdr;
    char recvbuf[MAXLINE];
    
    r = recvfrom(pg_sock, recvbuf, sizeof(recvbuf), 0, NULL, NULL);
    if (r < 0)
    {
        printf("recv_ping receive error!\n");
        exit(1);
    }
    
    //////////////////////////////////
    
    ip = (struct ip *)recvbuf;
    hlen1 = ip->ip_hl << 2;
    if (ip->ip_p != IPPROTO_ICMP)
        return;
    
    icmp = (struct icmp *)(recvbuf + hlen1);
    
    if (icmp->icmp_type == ICMP_ECHOREPLY)
    {
        if (icmp->icmp_id != pid)
            return;
        printf("icmp echo reply\n");
    }
    
    //////////////////////////////////
    
}


/*********************** check if visited this node before **************************/
//if return 1, visited before; else return 0
int check_visited(struct rt_datagram packet, int p)
{
    int i;
    for(i=0; i<p; i++)
    {
        if (packet.nodes[i].s_addr == my_addr.s_addr)
        {
            printf("有重复啦\n");
            return 1;
        }
    }
    return 0;
}


/**************** handle_rt() *****************/
//return 1 need to process(), return 0 not process()
int handle_rt(struct rt_datagram packet)
{
    struct msg last_send;
    
    if ((packet.pptr+1) == packet.len)
    {
        //last node, multicast first
        strcpy(last_send.buff, "This is Node ");
        strcpy(last_send.buff + 13, vmx);
        strcpy(last_send.buff + 13 + strlen(vmx), " Tour has ended. Group members please identify yourselves XXY");
        last_send.check_flag = 1;
        
        printf("Node %s sending < %s >\n", vmx, last_send.buff);
        mc_sendnum = sendto(sock_mc_send, &last_send, sizeof(struct msg), 0, sasend, salen);
        if (mc_sendnum < 0)
        {
            printf("mc sendto() at the last node error\n");
            exit(1);
        }
        return 0;
    }
    return 1;
}




/**************** handle_mc() *****************/
void handle_mc()
{
    if (recv_msg.check_flag == 1)
    {
        printf("Node %s Received < %s >\n", vmx, recv_msg.buff);
        memset(&recv_msg, 0, sizeof(struct msg));
        
        
        strcpy(send_msg.buff, "Node ");
        strcpy(send_msg.buff + 5, vmx);
        strcpy(send_msg.buff + 5 +strlen(vmx), " I am a member of the group XXY");
        send_msg.check_flag = 0;
        
        
        printf("Node %s sending < %s >\n", vmx, send_msg.buff);
        sendto(sock_mc_send, &send_msg, sizeof(struct msg), 0, sasend, salen);
    }
    
    if (strlen(recv_msg.buff) != 0)
    {
        printf("Node %s Received < %s >\n", vmx, recv_msg.buff);
        memset(&recv_msg, 0, sizeof(struct msg));
    }
}



/*********************** process packet **************************/
void process(struct rt_datagram recv_packet, int route_lenth, int rt_sock)
{
    struct rt_datagram send_packet;
    int i;
    int sendnum;
    struct sockaddr_in next;
    
    //host, len, pptr
    send_packet.host = my_addr;
    send_packet.len = route_lenth;
    send_packet.pptr = recv_packet.pptr + 1;
    
    printf("----------in process---------\n");
    
//    printf("send_packet.pptr: %d\n", send_packet.pptr);
    
    //nodes
    for (i=0; i<route_lenth; i++)
    {
//        printf("recv_packet.nodes[%d]: %s\n", i, inet_ntoa(recv_packet.nodes[i]));
        send_packet.nodes[i] = recv_packet.nodes[i];
    }
    
    //ip header
    send_packet.header.ip_v = 4;
    send_packet.header.ip_hl = 5;
    send_packet.header.ip_len = sizeof(struct rt_datagram);
    send_packet.header.ip_id = htons(IDENTIFICATION_TOUR);
    send_packet.header.ip_ttl = 1;
    send_packet.header.ip_p = IPPROTO_TOUR;
    send_packet.header.ip_src = my_addr;
    send_packet.header.ip_dst = send_packet.nodes[send_packet.pptr];
    printf("dst_addr: %s\n", inet_ntoa(send_packet.header.ip_dst));
    
    //init next node
    bzero(&next, sizeof(next));
    next.sin_family = AF_INET;
    next.sin_addr = send_packet.nodes[send_packet.pptr];
    
    //sendto
//    printf("sendto\n");
    sendnum = sendto(rt_sock, &send_packet, sizeof(send_packet), 0, (SA *)&next, sizeof(next));
    if (sendnum < 0)
    {
        printf("rt_sock in process error!n\n");
        exit(1);
    }
//    printf("sendnum: %d\n", sendnum);
    
    printf("----------end process---------\n");
}



/******************** mc_join() **************************/
void mc_join()
{
    sock_mc_send = Udp_client(MULTICAST_IP, MULTICAST_PORT, &sasend, &salen);
    sock_mc_recv = Socket(sasend->sa_family, SOCK_DGRAM, 0);
    Setsockopt(sock_mc_recv, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    sarecv = malloc(salen);
    memcpy(sarecv, sasend, salen);
    Bind(sock_mc_recv, sarecv, salen);
    Mcast_join(sock_mc_recv, sasend, salen, NULL, 0);
    Mcast_set_loop(sock_mc_send, 1);
}


/************************* main funtion *****************************/
int main (int argc, char ** argv)
{
    char node_pr[argc][INET_ADDRSTRLEN];  /*INET_ADDRSTRLEN for IPv4 dotted-decimal*/
    struct in_addr node_ip[argc];
    int i;
    struct hostent *hstp_temp;
    struct in_addr inaddr_temp;
    struct hwa_info *hwa_head, *hwa_cursor;
    

    
    
    struct rt_datagram packet;
    struct sockaddr_in rt_next;
    int sendnum;
    int recvnum;
    
    
    int maxfd;
    fd_set rset;
    
    int pfd[2];
    int pid;
    
    
    
    
//    printf("argc: %d\n", argc);
    
    //get my_addr
    hwa_head = get_hw_addrs();
    hwa_cursor = hwa_head;
    while(hwa_cursor != NULL){
        if (strcmp(hwa_cursor->if_name, "eth0") == 0  &&  hwa_cursor->ip_alias != 1){
            strcpy(node_pr[0], inet_ntoa(((struct sockaddr_in *)(hwa_cursor->ip_addr))->sin_addr));
            my_addr = ((struct sockaddr_in *)(hwa_cursor->ip_addr))->sin_addr;
            my_if_index = hwa_cursor->if_index;
            
            for(i=0; i<6; i++)
            {
                my_if_hwaddr[i] = hwa_cursor->if_haddr[i];
            }
            
            break;
        }
        hwa_cursor = hwa_cursor->hwa_next;
    }
    printf("ip addr in presentation: %s \n", node_pr[0]);
    
    
    
    // get the ip list(struct in_addr) of vm* tour
    node_ip[0] = my_addr;
    for(i=1; i<argc; i++)
    {
        hstp_temp = gethostbyname(argv[i]);
        inaddr_temp.s_addr = *((int *)hstp_temp->h_addr_list[0]);
        //       memset(node_ip[i-1], 0, INET_ADDRSTRLEN));
        strcpy(node_pr[i], inet_ntoa(inaddr_temp));
        node_ip[i] = inaddr_temp;
        printf("ip addr in presentation: %s \n", node_pr[i]);
    }
    if (node_ip[0].s_addr == node_ip[1].s_addr)
    {
        printf("The source node should not be part of the tour!\n");
        exit(1);
    }
        
    //'pg' IP raw socket
    if ((pg_sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0)
    {
        printf("rt_sock error!\n");
        exit(1);
    }
        
    //'rt' IP raw socket
    if ((rt_sock = socket(AF_INET, SOCK_RAW, IPPROTO_TOUR)) < 0)
    {
        printf("rt_sock error!\n");
        exit(1);
    }
    if (setsockopt(rt_sock, IPPROTO_IP, IP_HDRINCL, &on, sizeof(on)) < 0)
    {
        printf("setsockopt() for IP_HDRINCL error!\n");
        exit(1);
    }
     
    if (argc > 1)
    {
        //parameter in header
        memset(&packet, 0, sizeof(packet));
        //host, len, pptr
        packet.host = my_addr;
        packet.len = argc;
        packet.pptr = 1;
        //nodes
        for (i=0; i<argc; i++)
        {
            packet.nodes[i] = node_ip[i];
        }
        //ip header
        packet.header.ip_v = 4;
        packet.header.ip_hl = 5;
        packet.header.ip_len = sizeof(struct rt_datagram);
        packet.header.ip_id = htons(IDENTIFICATION_TOUR);
        packet.header.ip_ttl = 1;
        packet.header.ip_p = IPPROTO_TOUR;
        packet.header.ip_src = my_addr;
        packet.header.ip_dst = packet.nodes[1];
        //mac_ip and mac_port
        strcpy(packet.mac_ip, MULTICAST_IP);
        packet.mac_port = atoi(MULTICAST_PORT);
        
        //init next node
        bzero(&rt_next, sizeof(rt_next));
        rt_next.sin_family = AF_INET;
        rt_next.sin_addr = packet.nodes[1];
        
        //send rt_datagram
        sendnum = sendto(rt_sock, &packet, sizeof(packet), 0, (SA *)&rt_next, sizeof(rt_next));
        if (sendnum < 0)
        {
            printf("rt_socket send error!\n");
            exit(1);
        }
        
        
        //init node also need to add to multicast group
        mc_join();
        
    }
    
    
    h = gethostbyaddr((char *)&my_addr, sizeof(struct in_addr), AF_INET);
    strcpy(vmx, h->h_name);
    vmx[4] = 0; //null terminate
    
    
    while(1)
    {
        maxfd = 0;
        FD_ZERO(&rset);
        FD_SET(rt_sock, &rset);
        FD_SET(sock_mc_recv, &rset);
        FD_SET(pg_sock, &rset);
        
        maxfd = rt_sock>sock_mc_recv?rt_sock:sock_mc_recv;
        maxfd = pg_sock>maxfd?pg_sock:maxfd;
        maxfd = maxfd+1;
        switch (select(maxfd, &rset, NULL, NULL, NULL))
        {
            case -1:
                printf("select error\n");
                exit(1);
                break;
            case 0:
                break;
            default:
                //rt_sock
                if (FD_ISSET(rt_sock, &rset))
                {
//                    printf("get a input from rt_sock~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n");
                    recvnum = recvfrom(rt_sock, &packet, sizeof(packet), 0, NULL, NULL);
                    if (recvnum < 0)
                    {
                        printf("rt_socket recvfrom error!\n");
                        exit(1);
                    }
//                    printf("rt_sock recvnum: %d\n", recvnum);
                    
                    //not my packet
                    if (ntohs(packet.header.ip_id) != IDENTIFICATION_TOUR)
                    {
                        break;
                    }
                    
                  //  ticks = time(NULL);
                  //  snprintf(timebuf, sizeof(timebuf), "%.24s\r\n", ctime(&ticks));
                    printf("received source routing packet from <%s>\n\n", gethostbyaddr((char *)&packet.host, sizeof(struct in_addr), AF_INET)->h_name);
                    
                    //ping;
                    //create a pipe
                    
                    
                    
                    //fork
                    if((pid = fork()) < 0){
                        printf("fork failed\n");
                        exit(0);
                    }
                    
                    //child process;
                    if(pid == 0){
                        ping(packet.host.s_addr, packet);
                    }
                    
                    //else, parent process;
                    
                    
                    printf("main pid: %d\n", pid);
                    if (check_visited(packet, packet.pptr) == 0)
                    {
                        mc_join();
                    }
                    
                    //include last node situation
                    if ( handle_rt(packet) == 1 )
                    {
                        printf("received source routing packet from <%s>\n", inet_ntoa(packet.host));
                        process(packet, packet.len, rt_sock);
                    }
                }
                
                //multicast. need to judge 'tour end' or 'I am a member of...'
                if ( FD_ISSET(sock_mc_recv, &rset) )
                {
                    recvnum = recvfrom(sock_mc_recv, &recv_msg, sizeof(recv_msg), 0, NULL, NULL);
                    if (recvnum < 0)
                    {
                        printf("sock_mc_recv recvfrom error!\n");
                        exit(1);
                    }
                    
                    handle_mc();
                }
                
                
                //ping return
                if ( FD_ISSET(pg_sock, &rset) )
                {
                    printf("pg fanhuile\n");
                    recv_ping();
                }

        }
    }
    
    
    
    
    return 0;
}




