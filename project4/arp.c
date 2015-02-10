
#include "unp.h"
#include "hw_addrs.h"

#include <sys/socket.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <string.h>

#define MYID 7033
#define extraID 1211
#define ETH_FRAME_LEN 100

struct myinterface{
    int IP;
    char MAC[6];
    struct myinterface * next;
};

struct cache{
    int IP;
    char MAC[6];
    int index;
    int hatype;
    int sockfd;
    struct cache * next;
};

struct ARPmsg{
    int ID;
    int op;             //0 for request, 1 for reply;
    char senderMAC[6];
    int senderIP;
    char targetMAC[6];
    int targetIP;
};

static struct myinterface myinterfaces, *ifcursor;
static struct cache cacheheader, *cachecursor, *temp;

static int primIP;
static int myindex;
static struct sockaddr_ll pfaddr;
static int sockpf;

static fd_set fds;
static int maxfdp;


void printmsg(char * buffer){
    struct ARPmsg *arpmsg;
    char * ptr;
    int i;
    struct in_addr inaddr;
    
    arpmsg = (struct ARPmsg *)(buffer + 14);
    
    printf("=======================================\n");
    
    printf("the Ethernet frame header:\n");
    printf("destination MAC: ");
    ptr = buffer;
    i = IF_HADDR;
    do{
        printf("%.2x%s", *ptr++ & 0xff, (i == 1) ? "\n" : ":");
    }while(--i > 0);
    printf("source MAC: ");
    ptr = buffer + ETH_ALEN;
    i = IF_HADDR;
    do{
        printf("%.2x%s", *ptr++ & 0xff, (i == 1) ? "\n" : ":");
    }while(--i > 0);
    
    printf("\n");
    
    printf("payload in the frame: \n");
    printf("senderMAC: ");
    ptr = arpmsg->senderMAC;
    i = IF_HADDR;
    do{
        printf("%.2x%s", *ptr++ & 0xff, (i == 1) ? "\n" : ":");
    }while(--i > 0);
    
    inaddr.s_addr = arpmsg->senderIP;
    printf("sender IP: %s\n", inet_ntoa(inaddr));
    
    
    printf("targetMAC: ");
    ptr = arpmsg->targetMAC;
    i = IF_HADDR;
    do{
        printf("%.2x%s", *ptr++ & 0xff, (i == 1) ? "\n" : ":");
    }while(--i > 0);
    
    inaddr.s_addr = arpmsg->targetIP;
    printf("sender IP: %s\n", inet_ntoa(inaddr));
    printf("=======================================\n\n");
    
}


/************************** sendARPrequest() ****************************/
void sendARPrequest(int targetIP){
    struct sockaddr_ll dest_addr;
    char * buffer;
    struct ethhdr * eh;
    struct ARPmsg *arpmsg;
    char src_mac[6], dest_mac[6];
    int i;
    
    int test;
    char * ptr;
    
    //printf("in sendARPrequest\n");
    
    //set the dest_addr struct;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sll_family = PF_PACKET;
    dest_addr.sll_protocol = htons(MYID);
    dest_addr.sll_ifindex = myindex;    //index;
    dest_addr.sll_hatype = 1;                     //arp hardware identifier is ethernet, ARPHRD_ETHER;
    dest_addr.sll_pkttype = PACKET_OTHERHOST;     //target is another host;
    dest_addr.sll_halen = ETH_ALEN;               //address length: 6;
    for(i=0;i<6;i++){
        dest_addr.sll_addr[i] = 0xff;
    }
    
    
    memcpy((void *)src_mac, (void *)(myinterfaces.next)->MAC, ETH_ALEN);
    for(i=0; i<6; i++){
        dest_mac[i] = 0xff;
    }
    
    //eth frame;
    buffer = malloc(ETH_FRAME_LEN);
    memset(buffer, 0, ETH_FRAME_LEN);
    //header;
    memcpy(buffer, dest_mac, ETH_ALEN);
    memcpy(buffer+ETH_ALEN, src_mac, ETH_ALEN);
    eh = (struct ethhdr *)buffer;
    eh->h_proto = htons(MYID);
    
    arpmsg = (struct ARPmsg *)(buffer + 14);
    arpmsg->ID = (int)extraID;
    arpmsg->op = 0;
    memcpy(arpmsg->senderMAC, src_mac, ETH_ALEN);
    arpmsg->senderIP = primIP;
    memcpy(arpmsg->targetMAC, dest_mac, ETH_ALEN);
    arpmsg->targetIP = targetIP;    
    
    
    test = sendto(sockpf, buffer, ETH_FRAME_LEN, 0, (SA *)&dest_addr, sizeof(dest_addr));
    if(test < 0){
        printf("sendto error in sendARPmsg(), errno: %d\n", errno);
        return;
    }
    printf("\nsend an ARP request.\n");
    printmsg(buffer);
}

/*************************** sendARPreply() *****************************/
void sendARPreply(char * targetMAC, int targetIP, char * senderMAC, int senderIP){
    struct sockaddr_ll dest_addr;
    char * buffer;
    struct ethhdr * eh;
    struct ARPmsg *arpmsg;
    
    struct in_addr inaddr;
    
    int i;
    
    int test;
    char *ptr;
    
    //printf("in sendARPreply\n");
    
    
    inaddr.s_addr = senderIP;
    //printf("senderIP, %s\n", inet_ntoa(inaddr));
    
    //set the dest_addr struct;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sll_family = PF_PACKET;
    dest_addr.sll_protocol = htons(MYID);
    dest_addr.sll_ifindex = myindex;    //index;
    dest_addr.sll_hatype = 1;                     //arp hardware identifier is ethernet, ARPHRD_ETHER;
    dest_addr.sll_pkttype = PACKET_OTHERHOST;     //target is another host;
    dest_addr.sll_halen = ETH_ALEN;               //address length: 6;
    memcpy(dest_addr.sll_addr, targetMAC, ETH_ALEN);
    
    
    //eth frame;
    buffer = malloc(ETH_FRAME_LEN);
    memset(buffer, 0, ETH_FRAME_LEN);
    //header;
    memcpy(buffer, targetMAC, ETH_ALEN);
    memcpy(buffer+ETH_ALEN, senderMAC, ETH_ALEN);
    eh = (struct ethhdr *)buffer;
    eh->h_proto = htons(MYID);
    
    arpmsg = (struct ARPmsg *)(buffer + 14);
    arpmsg->ID = extraID;
    arpmsg->op = 1;             //reply
    memcpy(arpmsg->senderMAC, senderMAC, ETH_ALEN);
    arpmsg->senderIP = senderIP;
    memcpy(arpmsg->targetMAC, targetMAC, ETH_ALEN);
    arpmsg->targetIP = targetIP;
    
    
    test = sendto(sockpf, buffer, ETH_FRAME_LEN, 0, (SA *)&dest_addr, sizeof(dest_addr));
    if(test < 0){
        printf("sendto error in sendARPmsg(), errno: %d\n", errno);
        return;
    }
    
    printf("\nsend an ARP reply.\n");
    printmsg(buffer);
}

/************************** handleARPrequest() **************************/
void handleARPrequest(char * buffer){
    struct ARPmsg * arpmsg;
    
    int i;
    char * ptr;
    
    //printf("in handleARPrequest\n");
    arpmsg = (struct ARPmsg *)(buffer + 14);
    //check if this ARP pertains to this node or not;
    if(arpmsg->targetIP == primIP){
        //this is the target node;
        //update the cache;
        
        cachecursor = &cacheheader;
        while(cachecursor->next != NULL){
            if((cachecursor->next)->IP == arpmsg->senderIP){
                //update
                break;
            }
            cachecursor = cachecursor->next;
        }
        //there is no such entry;
        if(cachecursor->next == NULL){
            cachecursor->next = malloc(sizeof(struct cache));
            cachecursor = cachecursor->next;
            memset(cachecursor, 0, sizeof(struct cache));
            cachecursor->IP = arpmsg->senderIP;
            memcpy(cachecursor->MAC, arpmsg->senderMAC, ETH_ALEN);
        }
        
        sendARPreply(arpmsg->senderMAC, arpmsg->senderIP, (myinterfaces.next)->MAC, arpmsg->targetIP);
    }
}

/*************************** handleARPreply() ***************************/
void handleARPreply(char * buffer){
    char senderMAC[6];
    int senderIP;
    char targetMAC[6];
    int targetIP;
    struct ARPmsg *arpmsg;
    
    int test;
    struct in_addr inaddr;
    char * ptr;
    int i;
    
    //printf("in handleARPreply\n");
    
    arpmsg = (struct ARPmsg *)(buffer + 14);    
    
    
    inaddr.s_addr = arpmsg->senderIP;
    
    printf("senderIP: %s\n", inet_ntoa(inaddr));
    
    //check if there is this entry in the cache;
    cachecursor = cacheheader.next;
    while(cachecursor != NULL){
        
        inaddr.s_addr = cachecursor->IP;
        printf("IP in cache, %s\n", inet_ntoa(inaddr));
        
        if(cachecursor->IP == arpmsg->senderIP){
            //update the entry;
            printf("in if\n");
            memcpy(cachecursor->MAC, arpmsg->senderMAC, ETH_ALEN);
            //printf("mac len in handleARPreply: %d\n", strlen(cachecursor->MAC));
            
            //reply back to the TOUR through UNIX domain socket;
            test = send(cachecursor->sockfd, arpmsg->senderMAC, ETH_ALEN, 0);
            if(test < 0){
                printf("send error, errno: %d\n", errno);
                exit(0);
            }
            printf("send %d bytes to TOUR\n", test);
            
            cachecursor->sockfd = -1;
            return;
        }
        cachecursor = cachecursor->next;
    }    
}

/***************************** handleARP() ******************************/
void handleARP(int sockpf){
    struct ARPmsg * arpmsg;
    char * buffer;
    int test;
    
    //printf("in handleARP\n");
    
    buffer = malloc(ETH_FRAME_LEN);
    memset(buffer, 0, ETH_FRAME_LEN);
    
    test = recvfrom(sockpf, buffer, ETH_FRAME_LEN, 0, NULL, NULL);
    if(test < 0){
        printf("recvfrom error in handleARP(), errno: %d\n", errno);
        return;
    }
    
    
    arpmsg = (struct ARPmsg *)(buffer+14);
    
    //check the extraID;
    if(arpmsg->ID != extraID){
        return;
    }
    //check if the msg is a request or reply;
    if(arpmsg->op == 0){
        //it is a request;
        
        printf("receive a ARP request.\n");
        printmsg(buffer);
        handleARPrequest(buffer);
    }
    if(arpmsg->op == 1){
        //it is a reply;
        
        printf("receive a ARP reply.\n");
        printmsg(buffer);
        handleARPreply(buffer);
    }
}

/**************************** handleTOUR() ******************************/
void handleTOUR(int sockud){
    struct sockaddr_un touraddr;
    
    struct in_addr inaddr;
    int connsockud;
    char buff[MAXLINE];
    int * recvdIP;
    int test, size;
    
    //accept;
    size = sizeof(touraddr);
    //printf("waiting for a connection...\n");
    connsockud = accept(sockud, (struct sockaddr *)&touraddr, &size);
    if(connsockud < 0){
        printf("accept error, errno: %d\n", errno);
    }
    
    //recv from TOUR;
    test = recv(connsockud, (char *)buff, MAXLINE, 0);
    if(test < 0){
        printf("recv error, errno: %d\n", errno);
        exit(0);
    }
    recvdIP = (int *)buff;
    inaddr.s_addr = *recvdIP;
    //printf("recvd IP addrs: %s\n", inet_ntoa(inaddr));
    
    //check if there is already an entry in the list;
    cachecursor = &cacheheader;
    while(cachecursor->next != NULL){
        //already have a entry in the cache;
        if((cachecursor->next)->IP == *recvdIP){
            //send MAC back to TOUR
            return;
        }
        cachecursor = cachecursor->next;
    }
    
    //there is no such entry in the cache:
    //    1) make an imcomplete entry;
    //    2) send out an ARP request message;
    cachecursor->next = malloc(sizeof(struct cache));
    cachecursor = cachecursor->next;
    memset(cachecursor, 0, sizeof(struct cache));
    cachecursor->IP = *recvdIP;             //1
    cachecursor->sockfd = connsockud;        //5
    //printf("connsockud: %d\n", connsockud);
    sendARPrequest(*recvdIP);
}

/******************************* main() *********************************/
int main(int argc, char **argv){
    struct hwa_info *myhwa_info, *current;
    struct in_addr inaddr;
    int i, test;
    
    char * ptr;
    
    struct sockaddr_un arpaddr;
    int size;
    int sockud, connsockud;
    char buff[MAXLINE];
    
    //get the interfaces information;
    myhwa_info = get_hw_addrs();
    current = myhwa_info;
    memset(&myinterfaces, 0, sizeof(myinterfaces));
    ifcursor = &myinterfaces;
    
    while(current != NULL){
        if(strcmp(current->if_name, "eth0") == 0){
            ifcursor->next = malloc(sizeof(struct myinterface));
            ifcursor = ifcursor->next;
            memset(ifcursor, 0, sizeof(struct myinterface));
            ifcursor->IP = (((struct sockaddr_in *)(current->ip_addr))->sin_addr).s_addr;
            primIP = ifcursor->IP;
            memcpy(ifcursor->MAC, current->if_haddr, ETH_ALEN);
            myindex = current->if_index;
            inaddr.s_addr = ifcursor->IP;
            printf("IP: %s\n", inet_ntoa(inaddr));
            
            ptr = ifcursor->MAC;
            i = IF_HADDR;
            printf("MAC: ");
            do{
                printf("%.2x%s", *ptr++ & 0xff, (i == 1) ? "\n\n" : ":");
            }while(--i > 0);
        }
        
        else if (current->ip_alias == 1){
            ifcursor->next = malloc(sizeof(struct myinterface));
            ifcursor = ifcursor->next;
            memset(ifcursor, 0, sizeof(struct myinterface));
            ifcursor->IP = (((struct sockaddr_in *)(current->ip_addr))->sin_addr).s_addr;
            memcpy(ifcursor->MAC, current->if_haddr, ETH_ALEN);
            inaddr.s_addr = ifcursor->IP;
            printf("IP: %s\n", inet_ntoa(inaddr));
            
            ptr = ifcursor->MAC;
            i = IF_HADDR;
            printf("MAC :");
            do{
                printf("%.2x%s", *ptr++ & 0xff, (i == 1) ? "\n\n" : ":");
            }while(--i > 0);
        }
        current = current->hwa_next;
    }
    
    
    //initiate the cache list;
    memset(&cacheheader, 0, sizeof(cacheheader));
    
    //create a unix domain socket;
    unlink("arp_cse533-12");
    sockud = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&arpaddr, 0, sizeof(arpaddr));
    arpaddr.sun_family = AF_UNIX;
    strcpy(arpaddr.sun_path, "arp_cse533-12");
    //bind;
    test = bind(sockud, (struct sockaddr *)&arpaddr, sizeof(arpaddr));
    if(test < 0){
        printf("bind error, errno: %d\n", errno);
        exit(0);
    }
    //listen;
    test = listen(sockud, 5);
    if(test < 0){
        printf("listen error, errno: %d\n", errno);
        exit(0);
    }
    //create a PF_PACKET socket;
    sockpf = socket(AF_PACKET, SOCK_RAW, htons(MYID));
    if(sockpf < 0){
        printf("socket error, errno: %d\n", errno);
        exit(0);
    }
    //close(sockpf);
    memset(&pfaddr, 0, sizeof(pfaddr));
    pfaddr.sll_family = PF_PACKET;
    pfaddr.sll_protocol = htons(MYID);
    pfaddr.sll_ifindex = myindex;
    pfaddr.sll_hatype = 1;
    pfaddr.sll_pkttype = PACKET_HOST;
    pfaddr.sll_halen = ETH_ALEN;
    
    ptr = (myinterfaces.next)->MAC;
    for(i=0; i<ETH_ALEN; i++){
        pfaddr.sll_addr[i] = *ptr++ & 0xff;
    }
    //bind;
    test = bind(sockpf, (SA *)&pfaddr, sizeof(pfaddr));
    if(test < 0){
        printf("pf socket bind error, errno: %d\n", errno);
        exit(0);
    }
    
    
    //infinite for loop for select;
    for(;;){
        
        FD_ZERO(&fds);
        FD_SET(sockpf, &fds);
        FD_SET(sockud, &fds);
        
        
        maxfdp = sockpf>sockud?sockpf+1:sockud+1;
        /////////////////////////
        cachecursor = &cacheheader;
        while(cachecursor->next != NULL){
            if(strlen((cachecursor->next)->MAC) == 0){
                //printf("len: %d\n", strlen((cachecursor->next)->MAC) );
                connsockud = (cachecursor->next)->sockfd;
                
                if(connsockud < 0){
                    continue;
                }
                FD_SET(connsockud, &fds);
                maxfdp = maxfdp>connsockud+1?maxfdp:connsockud+1;
                break;
            }
            cachecursor = cachecursor->next;
        }
        /////////////////////////
        switch(select(maxfdp, &fds, NULL, NULL, NULL)){
            case -1:
                printf("select error!\n");
                break;
                
            default:
                //PF_PACKET socket input;
                if(FD_ISSET(sockpf, &fds)){
                    //handle ARP request or reply;
                    handleARP(sockpf);
                    break;
                }
                
                //unix domain socket input;
                if(FD_ISSET(sockud, &fds)){
                    //handle request from TOUR module;
                    handleTOUR(sockud);
                    break;
                }
                
                
                //connsockud closed
                cachecursor = &cacheheader;
                while(cachecursor->next != NULL){
                    if(strlen((cachecursor->next)->MAC) == 0){
                        if(FD_ISSET((cachecursor->next)->sockfd, &fds)){
                            if((test = read((cachecursor->next)->sockfd, buff, MAXLINE)) == 0){
                                
                                printf("connsockud closed, TOUR timeout!!!!!!!!!!!!!!!!!!!!!\n");
                                close((cachecursor->next)->sockfd);
                                FD_CLR((cachecursor->next)->sockfd, &fds);
                                temp = cachecursor->next;
                                cachecursor->next = temp->next;
                                free(temp);
                                break;
                            }
                            
                        }
                        
                    }
                    cachecursor = cachecursor->next;
                }
                break;
                
                
        }
        
    }
    
}


























