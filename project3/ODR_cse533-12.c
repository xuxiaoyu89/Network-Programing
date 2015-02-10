#include "unp.h"
#include "hw_addrs.h"

#include <sys/socket.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <string.h>

#define MYID 7033
#define ETH_FRAME_LEN 150
#define ODRPATH "myodrpath"
#define SERVPATH "permenant_serverpath"

//interface table;
struct myifi {
    struct sockaddr_ll addr;
    int sockfd;
    struct myifi *next;
};

//msg table;
struct mymsgs{
    char msg[MAXLINE];
    int srcportnum;
    int destIP;
    struct mymsgs * next;
};

//RREQ table;
struct RREQs{
    int src_addr;
    int broadcast_id;
    int timestamp;
    struct RREQs * next;
};

//RREP table;
struct RREPs{
    int src_addr;
    int broadcast_id;
    int timestamp;
    struct RREPs * next;
};

//client and server ports table;
struct ports{
    int portnum;
    char sun_path[21];
    int timestamp;
    struct ports * next;
};

//routing table;
struct routingtable{
    int destIP;
    char next_hop_mac[6];
    int ifindex;
    int hop_cnt;
    int timestamp;
    struct routingtable * next;
}; 

//struct of RREQ payload(14~ bytes in the frame);
struct RREQHEADER{
    int type;
    int redisflag;
    int RREP_ALREADY_SENT;
    int src_addr;            //IP addr;
    int dest_addr;
    int hop_cnt;
    int broadcast_id;
};

//struct of RREP payload(14~ bytes in the frame);
struct RREPHEADER{
    int type;
    int redisflag;
    int src_addr;            //IP addr;
    int dest_addr;
    int hop_cnt;
    int broadcast_id;
};

//struct of MSG payload(14~ bytes in the frame);
struct MSGHEADER{
    int type;
    int src_addr;
    int src_port;
    int dest_addr;
    int dest_port;
    int hop_cnt;
    char msg[150];
};

static struct myifi myifiheader, *myificursor;       //header and cursor of the interface table;
static struct ports portsheader, *portscursor;        //header and cursor of the sun_path table;
static struct mymsgs mmheader, *mmcursor, *mmtail;   //header, cursor and tail of the msg table;   //the tail always points to last msg in the table;
static struct RREQs recvdrreqheader, *recvdrreqcursor; //header and cursor of the RREQ table; ..........in recvdrreqheader, we don't store RREQ;
static struct RREPs recvdrrepheader, *recvdrrepcursor; //same with RREQ;
static struct routingtable rtheader, *rtcursor;           //header and cursor of the routing table;.....in rtheader, we don't store entry;

static int udsockfd;                    //unix domain socket;
static struct sockaddr_un udaddr;       //unix domain addr of odr;
static struct sockaddr_un servaddr;     //unix domain addr of server;

static int staleness;

static int primIP;                            //primary IP of the node;
static int cliportnum;                        //client port number;
static int mybroadcast_id;

void printif(){
    char * ptr;
    int i;
    
    struct myifi * ifitempcursor;
    
    printf("\n**********ifis***********\n");
    
    ifitempcursor = &myifiheader;
    while(ifitempcursor != NULL){
        ptr = (ifitempcursor->addr).sll_addr;
        i = IF_HADDR;
        do{
            printf("%.2x%s", *ptr++ & 0xff, (i == 1) ? " \n" : ":");
        }while(--i > 0);
        
        ifitempcursor = ifitempcursor->next;
    }
    
    printf("\n*************************\n");
}



/************************* print function *****************************/
print_info(char *buffer)
{
    int type;           //输出type类型用
    char mac[6];       //下一跳mac地址
    char *ptr;
    int src_addr;       //源ip地址
    int des_addr;       //目的ip地址
    int i = IF_HADDR;
    
    struct hostent he;
    struct hostent *phe;
    struct in_addr inaddr_src, inaddr_node, inaddr_des;
    
    
    printf("======================================================\n");
    
    ptr = &(mac[0]);
    
    phe = &he;
    
    //frame hdr infor can be got derectly
    memcpy(&type, buffer + 14, 4);
    memcpy(&mac, buffer, 6);
    
    
    inaddr_node.s_addr = primIP;
 //   p_node = gethostbyaddr((char *)&inaddr_node, sizeof(inaddr_node), AF_INET);
 //   printf("%s\n", inet_ntoa(inaddr_node));
        
    if (type == 0)
    {
        memcpy(&src_addr, buffer + 26, 4);
        memcpy(&des_addr, buffer + 30, 4);
        
        inaddr_src.s_addr = src_addr;
        inaddr_des.s_addr = des_addr;
    }
    
    if (type == 1)
    {
        memcpy(&src_addr, buffer + 22, 4);
        memcpy(&des_addr, buffer + 26, 4);
        
        inaddr_src.s_addr = src_addr;
        inaddr_des.s_addr = des_addr;
    }
    
    if (type == 2)
    {
        memcpy(&src_addr, buffer + 18, 4);
        memcpy(&des_addr, buffer + 26, 4);
        
        inaddr_src.s_addr = src_addr;
        inaddr_des.s_addr = des_addr;
    }
    
    phe = gethostbyaddr((char *)&inaddr_node, sizeof(struct in_addr), AF_INET);
    printf("ODR at node %s: sending frame hdr src %s    dest ", phe->h_name, phe->h_name);
    do{
        printf("%.2x%s", *ptr++ & 0xff, (i == 1) ? " \n" : ":");
    }while(--i > 0);
    phe = gethostbyaddr((char *)&inaddr_src, sizeof(struct in_addr), AF_INET);
    printf("ODR msg type %d    src %s    ", type, phe->h_name);
    phe = gethostbyaddr((char *)&inaddr_des, sizeof(struct in_addr), AF_INET);
    printf("dest %s\n", phe->h_name);
    
}




/*********************************** checkRREQ() *************************************/
//check if this RREQ has already been recvd and add it if not recvd before;
//return: 1 if has recvd before; 0 if has not recvd before; 
int checkRREQ(int src_addr, int broadcast_id){
    recvdrreqcursor = &recvdrreqheader;
    while(recvdrreqcursor->next != NULL){
        //if RREQ has been recvd before;
        if(((recvdrreqcursor->next)->src_addr == src_addr) && ((recvdrreqcursor->next)->broadcast_id == broadcast_id)){
            //printf("this RREQ has been recvd before!!! broadcast_id: %d\n", broadcast_id);
            //update the timestamp;
            (recvdrreqcursor->next)->timestamp = time(NULL);
            return 1;
        }
        recvdrreqcursor = recvdrreqcursor->next;
    }
    /*********if not recvd, add it to the list *********/
    //printf("this RREQ has not been recvd before!\n");
    //printf("the broadcast_id of this RREQ: %d\n", broadcast_id);
    recvdrreqcursor->next = malloc(sizeof(struct RREQs));
    memset(recvdrreqcursor->next, 0, sizeof(struct RREQs));
    recvdrreqcursor = recvdrreqcursor->next;
    recvdrreqcursor->src_addr = src_addr;
    recvdrreqcursor->broadcast_id = broadcast_id;
    recvdrreqcursor->timestamp = time(NULL);
    return 0;
}

int checkRREP(int src_addr, int broadcast_id){
    recvdrrepcursor = &recvdrrepheader;
    while(recvdrrepcursor->next != NULL){
        //if RREP has been recvd before;
        if(((recvdrrepcursor->next)->src_addr == src_addr) && ((recvdrrepcursor->next)->broadcast_id == broadcast_id)){
            //printf("this RREP has been recvd before!!! broadcast_id: %d\n", broadcast_id);
            //update the timestamp;
            (recvdrrepcursor->next)->timestamp = time(NULL);
            return 1;
        }
        recvdrrepcursor = recvdrrepcursor->next;
    }
    /********* if not recvd, add it to the list *********/
    //printf("this RREP has not been recvd before!\n");
    //printf("the broadcast_id of this RREP: %d\n", broadcast_id);
    recvdrrepcursor->next = malloc(sizeof(struct RREPs));
    memset(recvdrrepcursor->next, 0, sizeof(struct RREPs));
    recvdrrepcursor = recvdrrepcursor->next;
    recvdrrepcursor->src_addr = src_addr;
    recvdrrepcursor->broadcast_id = broadcast_id;
    recvdrrepcursor->timestamp = time(NULL);
    return 0;
}

/********************************** checkrtstale() ***********************************/
void checkrtstale(){
    //get the current time;
    int currenttime;
    int timestamp;
    struct routingtable * temp;
    struct in_addr inaddr;
    //test;
    int i;
    i = 0;
    currenttime = time(NULL);
    //printf("in checkrtstale\n");
    rtcursor = &rtheader;
    while(rtcursor->next != NULL){
        timestamp = (rtcursor->next)->timestamp;
        //if the route is stale, delete it;
        
        //printf("currenttime: %d\n", currenttime);
        //printf("timestamp: %d\n", timestamp);
        //printf("c-t = %d\n", currenttime - timestamp);
        if((currenttime - timestamp) >= staleness){
            temp = rtcursor->next;
            inaddr.s_addr = temp->destIP;
            rtcursor->next = temp->next;
            free(temp);
            //printf("delete stale route, destIP: %s\n", inet_ntoa(inaddr));
        }
        else{
            rtcursor = rtcursor->next;
        }
    }
}

/********************************* checkRREQstale() **********************************/
void checkRREQstale(){
    //get the current time;
    int currenttime;
    int timestamp;
    struct RREQs *temp;
    struct in_addr inaddr;
    currenttime = time(NULL);
    
    //printf("in checkRREQstale\n");
    recvdrreqcursor = &recvdrreqheader;
    while(recvdrreqcursor->next != NULL){
        timestamp = (recvdrreqcursor->next)->timestamp;
        //if the RREQ is stale, delete it;
        if((currenttime - timestamp) >= staleness){
            temp = recvdrreqcursor->next;
            inaddr.s_addr = temp->src_addr;
            //printf("delete stale RREQ, src_addr: %s, broadcast_id: %d\n", inet_ntoa(inaddr), temp->broadcast_id);
            recvdrreqcursor->next = temp->next;
            free(temp);
        }
        else{
            recvdrreqcursor = recvdrreqcursor->next;
        }
    }
}

/********************************* checkRREQstale() **********************************/
void checkRREPstale(){
    //get the current time;
    int currenttime;
    int timestamp;
    struct RREPs *temp;
    struct in_addr inaddr;
    currenttime = time(NULL);
    
    //printf("in checkRREPstale\n");
    recvdrrepcursor = &recvdrrepheader;
    while(recvdrrepcursor->next != NULL){
        timestamp = (recvdrrepcursor->next)->timestamp;
        //if the RREP is stale, delete it;
        if((currenttime - timestamp) >= staleness){
            temp = recvdrrepcursor->next;
            inaddr.s_addr = temp->src_addr;
            //printf("delete stale RREP, src_addr: %s, broadcast_id: %d\n", inet_ntoa(inaddr), temp->broadcast_id);
            recvdrrepcursor->next = temp->next;
            free(temp);
        }
        else{
            recvdrrepcursor = recvdrrepcursor->next;
        }
    }

}

/********************************* checkportstale() **********************************/
void checkportstale(){
    //get the current time;
    int currenttime;
    int timestamp;
    struct ports * temp;
    currenttime = time(NULL);
    //printf("in checkportstale\n");
    portscursor = &portsheader;
    while(portscursor->next != NULL){
        //printf("in while\n");
        timestamp = (portscursor->next)->timestamp;
        //if the RREQ is stale, delete it;
        if((currenttime - timestamp) >= staleness){
            //printf("current time: %d\n", currenttime);
            //printf("timestamp: %d\n", timestamp);
            //printf("portnum: %d\n", portscursor->portnum);
            temp = portscursor->next;
            portscursor->next = temp->next;
            free(temp);
            //printf("delete stale port!\n");
        }
        else{
            //printf("in else\n");
            portscursor = portscursor->next;
        }
    }
    //printf("out checkportstale\n");
}

/************************************ checkrt()***************************************/
//check if there is a route to destIP;
//return value: -1 if there is no route; hop_cnt of the route if there is a route;
int checkrt(int destIP){
    checkrtstale();
    rtcursor = rtheader.next;
    while(rtcursor != NULL){
        if(rtcursor->destIP == destIP){
            return rtcursor->hop_cnt;
        }
        rtcursor = rtcursor->next;
    }
    return -1;
}

/********************************** deletemsg() **************************************/
void deletemsg(int srcportnum, int destIP){
    struct mymsgs * temp;
    
    //printf("in deletemsg()\n");
    mmcursor = &mmheader;
    while(mmcursor->next != NULL){
        //find the msg to be deleted;
        if((mmcursor->next)->srcportnum == srcportnum && (mmcursor->next)->destIP == destIP){
            temp = mmcursor->next;
            mmcursor->next = temp->next;
            free(temp);
            return;
        }
        else mmcursor = mmcursor->next;
    }
}

/************************************ updatert() *************************************/
/*
 1) update: there is the entry and (redisflag ==1 or smaller hop_cnt), or there is no entry;
 2) do not update: redisflag == 0, and the entry has less hop_cnt;
 */
int updatert(int dest_addr, char * next_hop_mac, int index, int hop_cnt, int redisflag){
    /************* update the routing table ************/
    //first check if there is src_addr in the routing table;
    //if so, check hop_cnt and update accordingly;
    //if not, add the src_addr to the routing table!!!!!
    int i;
    struct in_addr inaddr;
    char * ptr;
    
    //printf("in updatert!\n");
    
    //test the input: next_hop_mac
    //printf("the input next hop mac\n");
    /*
    ptr = next_hop_mac;
    i = IF_HADDR;
    do{
        printf("%.2x%s", *ptr++ & 0xff, (i == 1) ? " \n" : ":");
    }while(--i > 0);
    */
    
    //before update, we should check the staleness of the routing table;
    //delete the stale route;
    checkrtstale();
    rtcursor = &rtheader;
    //update
    while(rtcursor->next != NULL){
        rtcursor = rtcursor->next;
        inaddr.s_addr = dest_addr;
        //printf("dest_addr: %s\n", inet_ntoa(inaddr));
        //there already a route to this node(src node);
        if(rtcursor->destIP == dest_addr){
            //if we get a shorter route or a route with the same hop_cnt, update the routing table;
            if((hop_cnt <= rtcursor->hop_cnt) || redisflag){
                if(redisflag == 1){
                    //printf("redisflag == 1, update the rt!!!\n");
                }
                //update next hop mac, ifi_index, hop_cnt, timestamp;
                strcpy(rtcursor->next_hop_mac, next_hop_mac);
                rtcursor->ifindex = index;
                rtcursor->hop_cnt = hop_cnt;
                rtcursor->timestamp = time(NULL);
                //printf("updated the timestamp, new timestamp: %d\n", rtcursor->timestamp);
                return 1;
            }
            else return 0;
            break;
        }
    }
    //printf("after while\n");
    //there is no src node in the routing table;
    if(rtcursor->next == NULL){
        rtcursor->next = malloc(sizeof(struct routingtable));
        rtcursor = rtcursor->next;
        memset(rtcursor, 0, sizeof(struct routingtable));
        rtcursor->destIP = dest_addr;
        memcpy(rtcursor->next_hop_mac, next_hop_mac, ETH_ALEN);
        rtcursor->ifindex = index;
        rtcursor->hop_cnt = hop_cnt;
        rtcursor->timestamp = time(NULL);
        //timestamp;
    }

    rtcursor = rtheader.next;
    /*
    printf("\nin routing table:\n");
    while (rtcursor != NULL) {
        i++;
        inaddr.s_addr = rtcursor->destIP;
        printf("IP: %s\n", inet_ntoa(inaddr));
        
        ptr = rtcursor->next_hop_mac;
        i = IF_HADDR;
        do{
            printf("%.2x%s", *ptr++ & 0xff, (i == 1) ? " \n" : ":");
        }while(--i > 0);
        rtcursor = rtcursor->next;
    }
    printf("\n");
     */
    //printif();
    //printf("out updatert!\n");
    
    
    return 1;
}

/************************************ deletert() *************************************/
//we use this function when rediscovery flag is set, delete the route to the destination;
void deletert(int dest_addr){
    struct routingtable * temp;
    
    //printf("in deletert()!\n");
    rtcursor = &rtheader;
    while(rtcursor->next != NULL){
        //find the entry;
        if((rtcursor->next)->destIP == dest_addr){
            temp = rtcursor->next;
            rtcursor->next = temp->next;
            free(temp);
            printf("delete one entry!\n");
            return;
        }
        else rtcursor = rtcursor->next;
    }
    //printf("no entry to delete!\n");
}

/************************************* flood() ***************************************/
//flood RREQ to its neighbours;
//last parameter index_in: the index of which we get the RREQ, we don't flood through this;
void flood(int srcIP, int destIP, int redisflag, int RREP_ALREADY_SENT, int hop_cnt, int broadcast_id, int index_in){
    //dest addr;
    struct sockaddr_ll dest_addr;
    //frame;
    char * buffer;
    //header of the frame;
    struct ethhdr * eh;
    char src_mac[6], dest_mac[6];
    //header of the RREQ msg;
    struct RREQHEADER *rreqheader;
    
    //temp cursor for myifi
    struct myifi *ifitempcursor;
    
    //test;
    struct in_addr inaddr;
    
    int i, sendnum;
    
    //printf("in flood\n");
    
    //set the dest_addr struct;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sll_family = PF_PACKET;
    dest_addr.sll_protocol = htons(MYID);
    //dest_addr.sll_ifindex = 3;    //index;
    dest_addr.sll_hatype = 1;                     //arp hardware identifier is ethernet, ARPHRD_ETHER;
    dest_addr.sll_pkttype = PACKET_OTHERHOST;     //target is another host;
    dest_addr.sll_halen = ETH_ALEN;               //address length: 6;
    
    dest_addr.sll_addr[0] = 0xFF;
    dest_addr.sll_addr[1] = 0xFF;
    dest_addr.sll_addr[2] = 0xFF;
    dest_addr.sll_addr[3] = 0xFF;
    dest_addr.sll_addr[4] = 0xFF;
    dest_addr.sll_addr[5] = 0xFF;
    
    dest_addr.sll_addr[6] = 0x00;
    dest_addr.sll_addr[7] = 0x00;
    
    
    //flood: send the frame to dest_addr from every interface;
    ifitempcursor = &myifiheader;
    //printf("in flood: before while loop\n");
    while(ifitempcursor != NULL){
        
        //we don't send through the interface from which we get the RREQ;
        //printf("current index: %d\n", (ifitempcursor->addr).sll_ifindex);
        //printf("index_in: %d\n", index_in);
        if((ifitempcursor->addr).sll_ifindex == index_in){
            //printf("in if\n");
            ifitempcursor = ifitempcursor->next;
            continue;
        }
        
        //dest_addr ifiindex: the outgoing index
        dest_addr.sll_ifindex = (ifitempcursor->addr).sll_ifindex;    //index;
        //printf("flood from interface %d\n", (ifitempcursor->addr).sll_ifindex);
        
        //init the frame;
        buffer = malloc(ETH_FRAME_LEN);
        memset(buffer, 0, ETH_FRAME_LEN);
        
        /*************************************************/
        //construct the frame for each interface;
        //condtruct the header of the frame;
        //printf("construct the frame\n");
        for(i=0; i<6; i++){
            src_mac[i] = (ifitempcursor->addr).sll_addr[i];
            dest_mac[i] = 0xff;
        }
        memcpy((void *)buffer, (void *)dest_mac, ETH_ALEN);
        memcpy((void *)(buffer + ETH_ALEN), (void *)src_mac, ETH_ALEN);
        eh = (struct ethhdr *)buffer;
        eh->h_proto = htons(MYID);
        
        //construct the header of RREQ;
        rreqheader = (struct RREQHEADER *)(buffer + 14);
        rreqheader->type = 0;
        rreqheader->redisflag = redisflag;
        rreqheader->RREP_ALREADY_SENT = RREP_ALREADY_SENT;
        rreqheader->src_addr = srcIP;
        rreqheader->dest_addr = destIP;
        rreqheader->hop_cnt = hop_cnt;                   //!!!!!!!!!!!!!!!!!
        rreqheader->broadcast_id = broadcast_id;              //!!!!!!!!!!!!!!!!!
        
        //test
        inaddr.s_addr = srcIP;
        //printf("srcIP: %s\n", inet_ntoa(inaddr));
        //printf("broadcast_id: %d\n", broadcast_id);
        
        /*************************************************/
        //sendto()
        sendnum = sendto(ifitempcursor->sockfd, buffer, ETH_FRAME_LEN, 0, (SA *)&dest_addr, sizeof(dest_addr));
        if(sendnum < 0){
            printf("sendto error in flood, errno: %d\n", errno);
            return;
        }
        
        print_info(buffer);

        ifitempcursor = ifitempcursor->next;
        //printf("flood %d bytes\n", sendnum);
        
    }
    //printf("out flood\n");
    
    //printif();
}

/************************************* sendRREP() ************************************/
void sendRREP(char *dest_mac, int index, int destIP, int srcIP, int hop_cnt, int redisflag, int broadcast_id){
    //dest addr;
    struct sockaddr_ll dest_addr;
    //frame;
    char * buffer;
    //header of the frame;
    struct ethhdr * eh;
    //src_mac;
    char src_mac[6];
    //header of the RREP msg;
    struct RREPHEADER *rrepheader;
    //temp cursor for myifi
    struct myifi *ifitempcursor;
    
    //the sockfd of the outgoing interface;
    int sockfd;
    //test;
    int sendnum;
    char * ptr;
    
    int i;
    
    //printf("in sendRREP!\n");
    
    
    //set the dest_addr struct;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sll_family = PF_PACKET;
    dest_addr.sll_protocol = htons(MYID);
    dest_addr.sll_hatype = 1;                     //arp hardware identifier is ethernet, ARPHRD_ETHER;
    dest_addr.sll_pkttype = PACKET_OTHERHOST;     //target is another host;
    dest_addr.sll_halen = ETH_ALEN;               //address length: 6;
    for(i=0;i<6;i++){
        dest_addr.sll_addr[i] = dest_mac[i]&0xFF;
    }
    dest_addr.sll_addr[6] = 0x00;
    dest_addr.sll_addr[7] = 0x00;
    
    
    //construct the RREP frame;
    buffer = malloc(ETH_FRAME_LEN);
    memset(buffer, 0, ETH_FRAME_LEN);
    //header;
    memcpy((void *)buffer, (void *)dest_mac, ETH_ALEN);
    ifitempcursor = &myifiheader;
    while (ifitempcursor != NULL) {
        if((ifitempcursor->addr).sll_ifindex == index){
            dest_addr.sll_ifindex = (ifitempcursor->addr).sll_ifindex;    //index;!!!!!!!!!!!!!!!!!!!!!!!!!!!
            //dest_addr.sll_ifindex = 4;    //index;!!!!!!!!!!!!!!!!!!!!!!!!!!!
            memcpy((void *)(buffer + ETH_ALEN), (ifitempcursor->addr).sll_addr, ETH_ALEN);
            sockfd = ifitempcursor->sockfd;
            break;
        }
        ifitempcursor = ifitempcursor->next;
    }
    eh = (struct ethhdr *)buffer;
    eh->h_proto = htons(MYID);
    
    //RREP payload;
    rrepheader = (struct RREPHEADER *)(buffer + 14);
    rrepheader->type = 1;
    rrepheader->redisflag = redisflag;
    rrepheader->src_addr = srcIP;
    rrepheader->dest_addr = destIP;
    rrepheader->hop_cnt = hop_cnt;
    rrepheader->broadcast_id = broadcast_id;
    
    //printf("in sendRREP(), hop_cnt: %d\n", hop_cnt);
    
    //send the frame through the rorresponding interface;
    sendnum = sendto(sockfd, buffer, ETH_FRAME_LEN, 0, (SA *)&dest_addr, sizeof(dest_addr));
    if(sendnum < 0){
        printf("in sendRREP, sendto error, errno: %d\n", errno);
        return;
    }
    print_info(buffer);
    //printf("send RREP, total %d bytes.\n", sendnum);
    //printif();
}

/********************************* sendcsmsg() ***************************************/
//deliver the MSG frame to the next hop;
void sendcsmsg(char * msg, int destIP, int dest_port, int srcIP, int src_port, int hop_cnt){
    //dest addr;
    struct sockaddr_ll dest_addr;
    //frame;
    char * buffer;
    //header of the frame;
    struct ethhdr * eh;
    //dest_mac
    char dest_mac[6];
    //src_mac;
    char src_mac[6];
    //header of the RREP msg;
    struct MSGHEADER *msgheader;
    //temp cursor for myifi
    struct myifi *ifitempcursor;

    //the sockfd of the outgoing interface;
    int sockfd;
    //the index of the outgoing interface;
    int index;
    //test;
    int sendnum;
    char * ptr;
    
    int i;
    
    
    //printf("in sendcsmsg\n");
    
    
    //missing dest_mac, and index;
    //get this two argument from routing table with destIP;
    rtcursor = rtheader.next;
    while(rtcursor != NULL){
        //find the route;
        if(rtcursor->destIP == destIP){
            memcpy(dest_mac, rtcursor->next_hop_mac, ETH_ALEN);
            index = rtcursor->ifindex;
            break;
        }
        rtcursor = rtcursor->next;
    }
    
    //if the route has been stale, so we should flood RREQ again to find the route;
    if(rtcursor == NULL){
        //printf("in sendcsmsg(), routing table have no entry, flood RREQ again to find route.\n");
        flood(srcIP, destIP, 0, 0, 0, ++mybroadcast_id, 0);     //hop_cnt = 0????
        return;
    }
    
    //set the dest_addr struct;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sll_family = PF_PACKET;
    dest_addr.sll_protocol = htons(MYID);
    dest_addr.sll_hatype = 1;                     //arp hardware identifier is ethernet, ARPHRD_ETHER;
    dest_addr.sll_pkttype = PACKET_OTHERHOST;     //target is another host;
    dest_addr.sll_halen = ETH_ALEN;               //address length: 6;
    for(i=0;i<6;i++){
        dest_addr.sll_addr[i] = dest_mac[i]&0xFF;
    }
    dest_addr.sll_addr[6] = 0x00;
    dest_addr.sll_addr[7] = 0x00;
    
    //construct the MSG frame;
    buffer = malloc(ETH_FRAME_LEN);
    memset(buffer, 0, ETH_FRAME_LEN);
    //dest_mac;
    memcpy((void *)buffer, (void *)dest_mac, ETH_ALEN);
    //src_mac, index, sockfd;
    ifitempcursor = &myifiheader;
    while (ifitempcursor != NULL) {
        if((ifitempcursor->addr).sll_ifindex == index){
            dest_addr.sll_ifindex = (ifitempcursor->addr).sll_ifindex;    //index;!!!!!!!!!!!!!!!!!!!!!!!!!!!
            //dest_addr.sll_ifindex = 4;    //index;!!!!!!!!!!!!!!!!!!!!!!!!!!!
            memcpy((void *)src_mac, (ifitempcursor->addr).sll_addr, ETH_ALEN);
            sockfd = ifitempcursor->sockfd;
            //break;
        }
        
        /*
        printf("from sendcsmsg: the src_mac:\n");
        ptr = (ifitempcursor->addr).sll_addr;
        i = IF_HADDR;
        do{
            printf("%.2x%s", *ptr++ & 0xff, (i == 1) ? " \n" : ":");
        }while(--i > 0);
        */
        
        
        ifitempcursor = ifitempcursor->next;
    }
    memcpy(buffer+ETH_ALEN, src_mac, ETH_ALEN);    
    
    
    eh = (struct ethhdr *)buffer;
    eh->h_proto = htons(MYID);
    
    //the payload of the frame;
    msgheader = (struct MSGHEADER *)(buffer + 14);
    msgheader->type = 2;
    msgheader->src_addr = srcIP;
    msgheader->src_port = src_port;
    msgheader->dest_addr = destIP;
    msgheader->dest_port = dest_port;
    msgheader->hop_cnt = hop_cnt;
    strcpy((char *)msgheader->msg, msg);
    
    sendnum = sendto(sockfd, buffer, ETH_FRAME_LEN, 0, (SA *)&dest_addr, sizeof(dest_addr));
    if(sendnum < 0){
        printf("in sendcsmsg, sendto error, errno: %d\n", errno);
        return;
    }
    print_info(buffer);
    //printf("send msg to client or server, total %d bytes.\n", sendnum);
    //printif();
}

/******************************** sendtoserver() *************************************/
void sendtoserver(char * msg, int srcIP, int src_port){
    
    
    int sendnum;
    char buffer[150];
    memcpy((void *)buffer, (void *)&srcIP, 4);
    memcpy((void *)(buffer+4), (void *)&src_port, 4);
    memcpy(buffer+8, msg, strlen(msg));
    
    sendnum = sendto(udsockfd, buffer, 150, 0, (SA *)&servaddr, sizeof(servaddr));
    if(sendnum < 0){
        printf("in sendtoserver(), sendto error, errno: %d\n", errno);
        return;
    }
    //printf("odr sends %d bytes to server\n", sendnum);
}

/********************************* sendtoclient() ************************************/
void sendtoclient(char * msg, int srcIP, int dest_port){
    struct sockaddr_un clientaddr;
    int sendnum;
    char buffer[150];
    int src_port = 0;        //src_port of server: 0;
    
    
    //construct the address of the destination client;
    //find the sun_path of the client in ports table with dest_port;
    clientaddr.sun_family = AF_LOCAL;
    portscursor = portsheader.next;
    while(portscursor != NULL){
        if(portscursor->portnum == dest_port){
            //printf("sun_path of the client: %s\n", portscursor->sun_path);
            strcpy(clientaddr.sun_path, portscursor->sun_path);
            clientaddr.sun_path[20] = '\0';
            break;
        }
        else
            portscursor = portscursor->next;
    }
    
    memcpy((void *)buffer, (void *)&srcIP, 4);
    memcpy((void *)(buffer+4), (void *)&src_port, 4);
    memcpy(buffer+8, msg, strlen(msg));
    
    sendnum = sendto(udsockfd, buffer, 150, 0, (SA *)&clientaddr, sizeof(struct sockaddr_un));
    if(sendnum < 0){
        printf("in sendtoclient(), sendto error, errno: %d\n", errno);
        return;
    }
    //printf("odr sends %d bytes to client\n", sendnum);
    
}

/******************************** handleclient() *************************************/
/*
 call this function if we get a msg from a client;
 here is what the function will do:
 1)get the infomation from the msg:
     1.destination IP address and port#;
     2.rediscovery flag;
     3.the msg to be sent;
 2)looking at the routing table, check if there is the requested IP address
 3)if there is the destination IP in the routing table, send the msg to the next hop;
   else, there is no such entry in the routing table, we flood a RREQ to its neighbours;
   and add the msg info into the msg table;
 */
void handleclient(char * msg, char * cli_sun_path){
    //get information from the msg;
    int srcportnum;          //the port num of the client;
    
    int destIP = *((int *)msg);
    int portnum = *((int *)(msg + 4));           //destination port num: 0
    int redisflag = *((int *)(msg + 8));
    char sendmsg[MAXLINE];
    char sun_path[21];
    struct in_addr inaddr;
    
    //printf("in handleclient\n");
    
    inaddr.s_addr = destIP;
    //printf("destIP: %s\n", inet_ntoa(inaddr));
    memcpy(sun_path, cli_sun_path, 20);
    sun_path[20] = '\0';
    strcpy(sendmsg, (char *)(msg + 12));
    sendmsg[4] = '\0';
    //printf("in handle client, sendmsg: %s\n", sendmsg);
    
    
    //check the staleness of the port table; delete the stale ones;
    checkportstale();
    //store the sun_path in the ports table;
    //delete the sun_path after certain amount of time;
    portscursor = &portsheader;
    while(portscursor != NULL){
        //from the same client;
        if(strcmp(portscursor->sun_path, sun_path) == 0){
            srcportnum = portscursor->portnum;           //set the client port num;
            //update the timestamp;
            portscursor->timestamp = time(NULL);
            break;
        }
        else{
            
            //printf("sun_path: %s\n", sun_path);
            //printf("sun_path in cursor: %s\n", portscursor->sun_path);
            if(portscursor->next == NULL){
                //printf("new port\n");
                portscursor->next = malloc(sizeof(struct ports));
                portscursor = portscursor->next;
                memset(portscursor, 0, sizeof(struct ports));
                //printf("portnum: %d\n", cliportnum);
                portscursor->portnum = cliportnum ++;
                srcportnum = portscursor->portnum;       //set the client port num; sent to the server;
                portscursor->timestamp = time(NULL);
                //printf("the timestamp of the new port: %d\n", portscursor->timestamp);
                strcpy(portscursor->sun_path, sun_path);
                portscursor->sun_path[20] = '\0';
                //printf("test11111111!!!!!!!!!!!!!!!!!\n");
                portscursor = portscursor->next;
                //printf("test11111111!!!!!!!!!!!!!!!!!\n");
                break;
            }
            else{
                portscursor = portscursor->next;
            }
        }
    }
    
    //check if the request of the client is to the server on this node;
    if(destIP == primIP){
        inaddr.s_addr = primIP;
        //printf("primIP: %s\n", inet_ntoa(inaddr));
        //printf("server at the same node, msg will be sent to the server!\n");
        //send msg to server;
        sendtoserver(sendmsg, primIP, srcportnum);
        return;
    }
    
    //add this msg to the msg table;
    mmcursor = &mmheader;
    while(mmcursor->next != NULL){
        mmcursor = mmcursor->next;
    }
    mmcursor->next = malloc(sizeof(struct mymsgs));
    mmcursor = mmcursor->next;
    memset(mmcursor, 0, sizeof(struct mymsgs));
    strcpy(mmcursor->msg, sendmsg);
    mmcursor->srcportnum = srcportnum;
    mmcursor->destIP = destIP;
    
    //print the msg table;
    mmcursor = mmheader.next;
    //printf("Here is the msgs in the msg table.\n");
    /*
    while(mmcursor != NULL){
        printf("%s\n", mmcursor->msg);
        mmcursor = mmcursor->next;
    }*/
    
    //check the staleness of the routing table, delete the stale ones;
    checkrtstale();
    
    //looking at the routing table;
    if(redisflag == 1){
        deletert(destIP);
    }
    rtcursor = rtheader.next;
    while(rtcursor != NULL){
        //there is the destIP in the routing table;
        //so we send the msg to the next hop;
        if(rtcursor->destIP == destIP){
            //sendmsg to next hop;
            //here we will use the srcportnum and primIP in the sendmsg(type 2 msg);
            //printf("there is already a route in the routing table.\n");
            sendcsmsg(sendmsg, destIP, portnum, primIP, srcportnum, 0);
            return;
        }
        //else;
        rtcursor = rtcursor->next;
    }
    
    //flood RREQ to the neighbours;
    //code here... // int srcIP, int destIP, int redisflag, int RREP_ALREADY_SENT, int hop_cnt, int broadcast_id, int index_in
    flood(primIP, destIP, redisflag, 0, 0, ++mybroadcast_id, 0);
    //printf("broadcast_id: %d\n", mybroadcast_id);
    //printf("out handleclient\n");
    //printif();
}

/********************************** handleserver() ***********************************/
void handleserver(char * msg){
    
    
    int destIP = *((int *)msg);
    int dest_port = *((int *)(msg + 4));
    int src_port = 0;
    char sendmsg[MAXLINE];
    
    int sendnum;
    
    //printf("in handleserver\n");
    
    //printf("dest_port: %d\n", dest_port);
    strcpy(sendmsg, msg+32);
    
    
    //check if server and client are at the same node;
    if(destIP == primIP){
        sendtoclient(sendmsg, primIP, dest_port);
        return;
    }

    sendcsmsg(sendmsg, destIP, dest_port, primIP, 0, 0);    
    
}

/********************************** handleRREQ() *************************************/
void handleRREQ(char * msg, int index){
    struct RREQHEADER * rreqheader;
    struct routingtable * temprtcursor;
    
    char src_mac[6];
    char dest_mac[6];
    
    int type;
    int redisflag;
    int RREP_ALREADY_SENT;
    int src_addr;            //IP addr;
    int dest_addr;
    int hop_cnt;             //hop_cnt: recv hop_cnt, real hop count number from the source to this node should be hop_cnt+1;
    int broadcast_id;
    struct in_addr inaddr;
    
    //
    int updatedflag;
    int reRREQflag;
    int route_hop_cnt;
    int src_route_hop_cnt;         //the hop_cnt of the route by the last RREQ; if we get the same RREQ multiple times,
                                   //we should check if the hop_cnt is the same, if the same, we will not flood.
    
    //test
    char * ptr;
    int i;
    
    //test;
    //printf("in handleRREQ\n");
    
    /**************** get the RREQ info ****************/
    //get the Frame Header;
    memcpy(dest_mac, msg, ETH_ALEN);
    memcpy(src_mac, msg + 6, ETH_ALEN);
    
    //printf("get payload\n");
    //get the RREQ payload;
    rreqheader = (struct RREQHEADER *)(msg + 14);
    type = rreqheader->type;
    redisflag = rreqheader->redisflag;
    RREP_ALREADY_SENT = rreqheader->RREP_ALREADY_SENT;
    src_addr = rreqheader->src_addr;
    dest_addr = rreqheader->dest_addr;
    hop_cnt = rreqheader->hop_cnt;
    broadcast_id = rreqheader->broadcast_id;
    
    inaddr.s_addr = src_addr;
    //printf("srcIP: %s\n", inet_ntoa(inaddr));
    //printf("broadcast_id: %d\n", broadcast_id);
    //printf("hop_cnt: %d\n", hop_cnt);
    
    /****** check if RREQ is sent from this node *******/
    //printf("before if\n");
    if(src_addr == primIP){
        //printf("src_addr == primIP\n");
        return;
    }
    
    //check the routing table, and delete the stale ones;
    
    
    
    /******* check if RREQ has been recvd before *******/
    //check the RREQ table, and delete the stale ones;
    checkRREQstale();
    //check if the RREQ has been recvd before; If not, add this RREQ to the table!!!!!!!!
    reRREQflag = checkRREQ(src_addr, broadcast_id);     //this function also add new RREQ to the table; print msg;
    
    //have recvd this RREQ before;
    if(reRREQflag == 1){
        if(redisflag == 1){
            deletert(dest_addr);
        }
        //check the RREQ gives a more efficient route;
        //get the previous hop_cnt if there is a same RREQ recvd before;
        src_route_hop_cnt = checkrt(src_addr);
        
        //if more efficient;
        if(hop_cnt+1 < src_route_hop_cnt){
            //printf("this RREQ gives a more efficient route!\n");
            updatert(src_addr, src_mac, index, hop_cnt+1, 0);
            //Send RREP;
            //if have reached the dest;
            if(dest_addr == primIP){
                //if RREP_ALREADY_SENT == 0, we can send RREP;
                if(RREP_ALREADY_SENT == 0){
                    sendRREP(src_mac, index, src_addr, dest_addr, 0, redisflag, broadcast_id); //redisflag!!!!!!!!!!!!!!!!!!!!
                    RREP_ALREADY_SENT = 1;
                }
                //else, don't send RREP;
                else{
                    //printf("RREP already sent!\n");
                }
            }
            //if it is not the destination;
            else{
                if(redisflag == 0){
                    //check if there is a route to the dest;
                    route_hop_cnt = checkrt(dest_addr);
                    
                    //send RREP if we get a route to the destination in the routing table, and the RREP_ALREADY_SENT flag is 0;
                    //and the hop_cnt of this RREQ is smaller than the previous one;
                    if(RREP_ALREADY_SENT == 0 && route_hop_cnt > 0){
                        sendRREP(src_mac, index, src_addr, dest_addr, route_hop_cnt, redisflag, broadcast_id);//redisflag!!!!!!!!!!!!!!!!!!!!
                        RREP_ALREADY_SENT = 1;
                    }
                }
                //else printf("redisflag == 1, we cannot send RREP here.\n");
            }
            //at last
            //if we recvd this RREQ before, and this time we have the same hop_cnt, we should update the routing table,
            //but we do not need to flood;
            
            flood(src_addr, dest_addr, redisflag, RREP_ALREADY_SENT, hop_cnt + 1, broadcast_id, index);
        }
        //else, this regot RREQ does not give a more efficient route, we will do nothing;
        //else printf("this RREQ does not gives a more efficient route. Do nothing\n");
    }
    //this is a new RREQ;
    else{
        if(redisflag == 1){
            deletert(dest_addr);
        }
        updatert(src_addr, src_mac, index, hop_cnt+1, redisflag);
        //send RREP;
        //if have reached the dest;
        if(dest_addr == primIP){
            //if RREP_ALREADY_SENT == 0, we can send RREP;
            if(RREP_ALREADY_SENT == 0){
                sendRREP(src_mac, index, src_addr, dest_addr, 0, redisflag, broadcast_id); //redisflag!!!!!!!!!!!!!!!!!!!!
                RREP_ALREADY_SENT = 1;
            }
            //else, don't send RREP;
            else{
                //printf("RREP already sent!\n");
            }
            
        }
        //else, this node is not dest;
        else{
            if(redisflag == 0){
                //check if there is a route to the dest;
                route_hop_cnt = checkrt(dest_addr);
                //send RREP if we get a route to the destination in the routing table, and the RREP_ALREADY_SENT flag is 0;
                if(RREP_ALREADY_SENT == 0 && route_hop_cnt > 0){
                    sendRREP(src_mac, index, src_addr, dest_addr, route_hop_cnt, redisflag, broadcast_id);//redisflag!!!!!!!!!!!!!!!!!!!!
                    RREP_ALREADY_SENT = 1;
                }
                if(RREP_ALREADY_SENT == 1){
                    //printf("RREP already sent!\n");
                }
            }
            //else if redisflag == 1, we do not send RREP;
            //else printf("redisflag == 1, we cannot send RREP here.\n");
        }
        //at last, in this situation, it is a new RREQ, it has to be flooded out;
        flood(src_addr, dest_addr, redisflag, RREP_ALREADY_SENT, hop_cnt + 1, broadcast_id, index);
    }
}

/********************************** handleRREP() *************************************/
void handleRREP(char * msg, int index){
    struct RREPHEADER * rrepheader;
    struct routingtable * temprtcursor;
    char src_mac[6];
    char dest_mac[6];
    
    int type;
    int redisflag;
    int src_addr;            //IP addr;
    int dest_addr;
    int hop_cnt;
    int broadcast_id;
    int outgoingindex;
    
    int updatedflag;
    int reRREPflag;
    int route_hop_cnt;
    //test
    int i;
    struct in_addr inaddr;
    char * ptr;
    struct mymsgs * temp;
    
    //printf("in handleRREP!\n");
    
    /**************** get the RREP info ****************/
    
    memcpy(src_mac, msg + ETH_ALEN, ETH_ALEN);
    
    //get the RREQ payload;
    rrepheader = (struct RREPHEADER *)(msg + 14);
    type = rrepheader->type;
    redisflag = rrepheader->redisflag;
    src_addr = rrepheader->src_addr;
    dest_addr = rrepheader->dest_addr;
    hop_cnt = rrepheader->hop_cnt;
    broadcast_id = rrepheader->broadcast_id;
    
    inaddr.s_addr = dest_addr;
    //printf("dest addr: %s\n", inet_ntoa(inaddr));
    
    
    /****************** check if the RREP has been recvd before ********************/
    //check the RREP table, and delete the stale ones;
    checkRREPstale();
    //check if the RREP has been recvd before; If not, add this RREP to the table!!!!!!!!
    reRREPflag = checkRREP(dest_addr, broadcast_id);
    
    if(reRREPflag == 1){
        route_hop_cnt = checkrt(src_addr);
        //this RREP gives a more efficient route;
        if(hop_cnt+1 <= route_hop_cnt){
            //update the routing table
            updatedflag = updatert(src_addr, src_mac, index, hop_cnt+1, 0);
            //check if this node is the destination node;
            if(dest_addr == primIP){
                //printf("RREP reached the destinaion!\n");
                //printf("this is not the first RREP for the same RREQ, msg already been sent.");
                return;
            }
            
            /********** not dest node, send a RREP to the next hop **********/
            //looking at the routing table and get the mac_addr of the next hop;
            //if the relative route is has been stale at this time, so we cannot find a route to the client;
            //in this situation, we should flood RREQ again to find the route;
            
            temprtcursor = rtheader.next;
            while (temprtcursor != NULL) {
                if(temprtcursor->destIP == dest_addr){
                    //printf("find!!!\n");
                    break;
                }
                else
                    temprtcursor = temprtcursor->next;
            }
            
            if(temprtcursor == NULL){
                //printf("in handleRREP(), routing table have no entry, flood RREQ again to find route.\n");
                flood(dest_addr, src_addr, 0, 0, 0, ++mybroadcast_id, 0);     //hop_cnt = 0????
                return;
            }
            outgoingindex = temprtcursor->ifindex;
            memcpy(dest_mac, temprtcursor->next_hop_mac, ETH_ALEN);
            
            /*
            ptr = dest_mac;
            i = IF_HADDR;
            do{
                printf("%.2x%s", *ptr++ & 0xff, (i == 1) ? " " : ":");
            }while(--i > 0);
            */
            sendRREP(dest_mac, outgoingindex, dest_addr, src_addr, hop_cnt + 1, redisflag, broadcast_id);
            
        }
        
        //else, this regot RREP does not give a more efficient route, we will do nothing;
        else{
            //printf("this RREP does not give a more efficient route.\n");
        }
    }
    
    
    //if this RREP is first recvd;
    else{
        
        updatert(src_addr, src_mac, index, hop_cnt+1, redisflag);
        //check if this node is the destination node;
        if(dest_addr == primIP){
            //check the msg table and send msg to the server;
            //test!!!
            //printf("RREP reached the destinaion!\n");
            //printf("hop_cnt: %d\n", hop_cnt);
            
            mmcursor = &mmheader;
            while(mmcursor->next != NULL){
                //find the msg to the src_addr(its destination IP);
                if((mmcursor->next)->destIP == src_addr){
                    temp = mmcursor->next;
                    //send msg to the destination;
                    //printf("the srcportnum of the msg: %d\n", temp->srcportnum);
                    sendcsmsg((char *)temp->msg, src_addr, 0, primIP, temp->srcportnum, 0);
                    //delete this msg entry from msg table;
                    mmcursor->next = temp->next;
                    free(temp);
                    return;
                }
                mmcursor = mmcursor->next;
            }
            
//            printf("bug!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
            return;
        }
        
        
        /********** not dest node, send a RREP to the next hop **********/
        //looking at the routing table and get the mac_addr of the next hop;
        
        //if the relative route is has been stale at this time, so we cannot find a route to the client;
        //in this situation, we should flood RREQ again to find the route;
        
        temprtcursor = rtheader.next;
        while (temprtcursor != NULL) {
            if(temprtcursor->destIP == dest_addr){
                //printf("find!!!\n");
                break;
            }
            else
                temprtcursor = temprtcursor->next;
        }
        
        if(temprtcursor == NULL){
            //printf("in handleRREP(), routing table have no entry, flood RREQ again to find route.\n");
            flood(dest_addr, src_addr, 0, 0, 0, ++mybroadcast_id, 0);     //hop_cnt = 0????
            return;
        }
        outgoingindex = temprtcursor->ifindex;
        memcpy(dest_mac, temprtcursor->next_hop_mac, ETH_ALEN);
        /*
        ptr = dest_mac;
        i = IF_HADDR;
        do{
            printf("%.2x%s", *ptr++ & 0xff, (i == 1) ? " " : ":");
        }while(--i > 0);
        */
        sendRREP(dest_mac, outgoingindex, dest_addr, src_addr, hop_cnt + 1, redisflag, broadcast_id);
        //printif();

    }
}

/*********************************** handleMSG() *************************************/
void handleMSG(char * msg, int index){
    struct MSGHEADER * msgheader;
    
    int type;
    int src_addr;
    int src_port;
    int dest_addr;
    int dest_port;
    int hop_cnt;
    char csmsg[150];
    
    char dest_mac[6];
    char src_mac[6];
    
    char * ptr;
    int i;
    
    memcpy(dest_mac, msg, ETH_ALEN);
    memcpy(src_mac, msg + ETH_ALEN, ETH_ALEN);
    
    //printf("in handleMSG()\n");
    
    //printf("the src_mac of the msg frame:\n");
    /*
    ptr = src_mac;
    i = IF_HADDR;
    do{
        printf("%.2x%s", *ptr++ & 0xff, (i == 1) ? " \n" : ":");
    }while(--i > 0);
    */
    
    
    msgheader = (struct MSGHEADER *)(msg + 14);
    src_addr = msgheader->src_addr;
    src_port = msgheader->src_port;
    dest_addr = msgheader->dest_addr;
    dest_port = msgheader->dest_port;
    hop_cnt = msgheader->hop_cnt;
    strcpy(csmsg, msgheader->msg);
    
    //update the routing table with src_addr;
    updatert(src_addr, src_mac, index, hop_cnt + 1, 0);

    //check if the msg reaches its destination;
    //reaches its destination: send to the server/client process;
    if(dest_addr == primIP){
        //test: check the hop_cnt;
        //printf("the hop_cnt is: %d\n", hop_cnt + 1);
        //check the portnum: 1)dest_port != 0, from server, send the msg to client;
        //                   2)dest_port == 0, from client, send the msg to server;
        //send to server;
        if(dest_port == 0){
            //printf("the msg reaches its destination, will be sent to the server!\n");
            //send msg to server;
            sendtoserver(csmsg, src_addr, src_port);
        }
        
        //send to client;
        else{
            //printf("the msg reaches its destination, will be sent to the client!\n");
            //printf("the src_port of the msg: %d\n", src_port);
            sendtoclient(csmsg, src_addr, dest_port);
            //delete this msg in msg table
            deletemsg(dest_port, src_addr);         //srcportnum: the port num of client; destIP, the destination IP of the msg;
        }
        return;
    }
    //not its destinatian: send this msg to next hop;
    //sendcsmsg()
    sendcsmsg(csmsg, dest_addr, dest_port, src_addr, src_port, hop_cnt + 1);
    //printif();
}

/********************************** main function ************************************/
int main(int argc, char **argv){
    
    struct hwa_info * myhwa_info, *current;
    char * buffer;
    int i;
    
    char src_mac[6], dest_mac[6], *data;
    struct ethhdr * eh;
    struct sockaddr_ll dest_addr;
    
    
    
    //define fd_set for select;
    fd_set fds;
    int maxfdp;
    
    //define a temp sockaddr_un: tempaddr_un;
    struct sockaddr_un tempaddr_un;
    
    //define a char [] to recv msg from udsockfd and sock_raw;
    char msg_seq[MAXLINE];
    char recvmsg[ETH_FRAME_LEN];
    char cssun_path[21];
    
    //recvnum, sendnum;
    int recvnum, sendnum;
    int structsize;
    
    //msg type from raw_socket
    int type;
    
    //test
    char * test;
    
    //initialize
    
    if (argc != 2){
        printf("lack of staleness parameter!\n");
        exit(1);
    }
    
    staleness = atoi(argv[1]);
    printf("staleness: %d\n", staleness);
    
    //assignment a portnum to a temp sun_path of a client;
    cliportnum = 1;
    mybroadcast_id = 0;
    
    memset(&recvdrreqheader, 0, sizeof(struct RREQs));
    memset(&recvdrrepheader, 0, sizeof(struct RREPs));
    memset(&rtheader, 0, sizeof(struct routingtable));
    memset(&mmheader, 0, sizeof(struct mymsgs));
    mmtail = &mmheader;
    
    /******************* build a list of sun_path ******************/
    strcpy(portsheader.sun_path, SERVPATH);
    portsheader.portnum = 0;
    
    
    /***************************************************************/
    //get the interface info, create a sockfd for each inferface
    //bind the sockfd and the sockaddr_ll
    myhwa_info = get_hw_addrs();
    current = myhwa_info;
    memset(&myifiheader, 0, sizeof(myifiheader));
    myificursor = &myifiheader;
    while(current != NULL){
        int prflag = 0;
        int i = 0;
        char * ptr;
        int bindnum;
        if((strcmp(current->if_name, "lo") == 0) ){
            //printf("ignore loopback interface!\n")
            current = current->hwa_next;
            continue;
        }
        if(strcmp(current->if_name, "eth0") == 0){
            //printf("ignore primary IP interface!\n");
            //get the primary IP address of this node;
            primIP = (((struct sockaddr_in *)(current->ip_addr))->sin_addr).s_addr;
            current = current->hwa_next;
            continue;
        }
        if(current->ip_alias == 1){
            //printf("ignore alias!\n");
            current = current->hwa_next;
            continue;
        }
        
        /*
        do{
            if(current->if_haddr[i] != '\0'){
                prflag = 1;
                break;
            }
        } while( ++i < IF_HADDR);
        if(prflag){
            //printf("HW addr = ");
            ptr = current->if_haddr;
            i = IF_HADDR;
            do{
                //printf("%.2x%s", *ptr++ & 0xff, (i == 1) ? " " : ":");
            }while(--i > 0);
        }
        
        //printf("\ninterface index: %d\n", current->if_index);
        //printf("\n");
         
         */
        (myificursor->addr).sll_family = PF_PACKET;             
        (myificursor->addr).sll_protocol = htons(MYID);         
        (myificursor->addr).sll_ifindex = current->if_index;    //index;
        (myificursor->addr).sll_hatype = 1;                     //ARPHRD_ETHER;
        (myificursor->addr).sll_pkttype = PACKET_HOST;          //target is another host;
        (myificursor->addr).sll_halen = ETH_ALEN;               //address length: 6;
        
        ptr = current->if_haddr;
        
        for(i=0; i<ETH_ALEN; i++){
            (myificursor->addr).sll_addr[i] = *ptr++ & 0xff;
        }
        
        myificursor->sockfd = socket(AF_PACKET, SOCK_RAW, htons(MYID));
        if(myificursor->sockfd < 0){
            printf("socket error, errno: %d\n", errno);
            exit(0);
        }
        
        bindnum = bind(myificursor->sockfd, (SA *)&(myificursor->addr), sizeof(struct sockaddr_ll));
        if(bindnum < 0){
            printf("PF_SOCKET bind error, errno: %d\n", errno);
            exit(0);
        }
        
        current = current->hwa_next;
        if(current != NULL){
            myificursor->next = malloc(sizeof(struct myifi));
            myificursor = myificursor->next;
            memset(myificursor, 0, sizeof(struct myifi));
        }
    }
    
    
    /************************************************************************/
    //bind the udsockfd and the udaddr;
    unlink(ODRPATH);
    udsockfd = socket(AF_LOCAL, SOCK_DGRAM, 0);
    bzero(&udaddr, sizeof(udaddr));
    udaddr.sun_family = AF_LOCAL;
    strcpy(udaddr.sun_path, ODRPATH);
    bind(udsockfd, (SA *)&udaddr, sizeof(udaddr));
    
    //initialize the server addr
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sun_family = AF_LOCAL;
    strcpy(servaddr.sun_path, SERVPATH);
    
    
    /*********************** select from the sockdfs ************************/    
    while(1){
        //printf("after while\n");
        //printif();
        
        FD_ZERO(&fds);
        FD_SET(udsockfd, &fds);
        maxfdp = udsockfd;
        myificursor = &myifiheader;
        while(myificursor != NULL){
            FD_SET(myificursor->sockfd, &fds);
            if(maxfdp < (myificursor->sockfd)){
                maxfdp = myificursor->sockfd;
            }
            myificursor = myificursor->next;
        }
        maxfdp ++;

        //printf("before select!\n");
        //printif();
        switch(select(maxfdp, &fds, NULL, NULL, NULL)){
            case -1:
                printf("select error!\n");
                exit(0);
                break;
                
            default:
                if(FD_ISSET(udsockfd, &fds)){
                    //addr: recv the addr of the server or client;
                    bzero(&tempaddr_un, sizeof(tempaddr_un));
                    structsize = sizeof(struct sockaddr_un);
                    //printf("unix domain input!!!\n");
                    recvnum = recvfrom(udsockfd, msg_seq, MAXLINE, 0, (SA *)&tempaddr_un, &structsize);
                    if(recvnum < 0){
                        printf("recv error, errno: %d\n", errno);
                        break;
                    }
                    //printf("recvnum: %d\n", recvnum);
                    
                    memcpy((void *)cssun_path, tempaddr_un.sun_path, 20);
                    cssun_path[20] = '\0';
                    //printf("sun_path of the client/server: %s\n", cssun_path);

                    i = strcmp(cssun_path, SERVPATH);
                    if(i == 0){
                        //printf("odr gets a msg from server\n");
                        handleserver((char *)msg_seq);
                    }
                    //the message is from a client
                    else{
                        //printf("msg from a client\n");
                        handleclient((char *)msg_seq, cssun_path);
                    }
                }
                
                //recv Ethernet Frame from RAW socket;
                myificursor = &myifiheader;
                //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
                while (myificursor != NULL) {
                    if(FD_ISSET(myificursor->sockfd, &fds)){
                        
                        //printf("ODR gets a sock_raw frame\n");
                        //test!!!!!!!!!!!
                        recvnum = recvfrom(myificursor->sockfd, recvmsg,  MAXLINE, 0, NULL, NULL);
                        if(recvnum < 0){
                            printf("recvfrom error! errno: %d\n", errno);
                            continue;
                        }
                        //check the type of the msg
                        type = *((int *)(recvmsg + 14));
                        //printf("type: %d\n", type);
                        //this is a RREQ
                        if(type == 0){
                            handleRREQ(recvmsg, (myificursor->addr).sll_ifindex);
                            //printf("after handleRREQ\n");
                        }
                        //this is a RREP
                        else if(type == 1){
                            handleRREP(recvmsg, (myificursor->addr).sll_ifindex);
                        }
                        //this is a message
                        else if(type == 2){
                            handleMSG(recvmsg, (myificursor->addr).sll_ifindex);
                        }
                    }
                    //printf("in main select: select sock_raw\n");
                    myificursor = myificursor->next;
                }
                break;
        }
    }
}









