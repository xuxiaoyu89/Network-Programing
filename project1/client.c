#include "unp.h"

#define INET_ADDRSTRLEN 16
#define MAXLINE 4096
#define SA struct sockaddr




static void * handleparentinput(void * args){
    
    fd_set fds;
    int selectout;
    struct timeval timeout = {0,0};
    int quitflag = 0;
    char errorbuff[10];
    
    for(;;){
        FD_ZERO(&fds);
        FD_SET(fileno(stdin), &fds);
        switch(select(fileno(stdin)+1, &fds, NULL, NULL, NULL)){
            case -1:
                exit(-1);
            case 0:
                break;
            default:
                if(FD_ISSET(fileno(stdin), &fds)){
                    
                    if(*(int *)args == 1){                                       //child ended;
                        quitflag = 1;
                        break;
                    }
                    else{
                        
                        /*
                         fgets(): everytime fgets() reads a line. 
                         But the first time I input something to the terminal, there is a '\n' in the beginning,
                         which is very confusing. So I write an "if" to ignore this '\n'.
                         Another thing is that after fgets() read one line, process goes to loop again, select()
                         will still return a val>0, until fgets() gets everything from stdin.
                         */
                        //if the first char in errorbuff is '\n', ignore it.
                        fgets(errorbuff,10,stdin);
                        if(errorbuff[0] == '\n'){
                            break;
                        }
                        printf("Wrong Command!!!\n");
                        printf("This is the parent process, please input in the xterm window.\n");
                        printf("If you are requiring a time service, you do not have to input anything.\n");
                        break;
                    }
                }
                break;
        }
        
        if(quitflag == 1){
            //quitflag = false;
            break;
        }
        
    }
    
    pthread_exit(NULL);
}


main(int argc, char ** argv) {
    
    
    
    char *ptr, **pptr;
    char  str[INET_ADDRSTRLEN];                                          //str is the "**.**.**.**" address;
    struct hostent *hptr;                                                //server address struct, (*hptr).
    socklen_t len;
    int ifquit = 0;

    ptr =  argv[1];
    
    pthread_t tid;                                            //parent process: handle input from user and information from child.
    
    
    if(argc != 2){
        printf("make sure you have input an IP address or a server name.\n");
    }

    
    
    char firstchar = ptr[0];
    
    //test the first character;
    //printf("%d", firstchar);  
    
    if(firstchar >= 48 && firstchar <= 57){                             //the inputted address is the dotted decimal address;
        //printf("it is a decimal dotted address!\n");
        struct in_addr addr;
        
        //get a in_addr struct(addr) for the first parameter in gethostbyaddr;
        if( inet_aton(ptr, &addr) == 0){                                //change the data type of ptr, from char* to in_addr;
            printf("the decimal dotted address is not a ipv4 address");
            //write a return function to return to a place where we ask user to re-enter the address; 
        }
        
        //gethostbyaddr, the host info is stored in hptr;
        if((hptr = gethostbyaddr(&addr, 4, AF_INET) ) == NULL ){        //ptr should be a in_addr structure; but here it is a char*;
             printf("gethostbyname error for host: %s\n", ptr);
        }
        
        switch((*hptr).h_addrtype){
            case AF_INET:
                pptr = (*hptr).h_addr_list;
                inet_ntop( (*hptr).h_addrtype, *pptr, str, sizeof(str));
                printf("IP address: %s\n", str);
                char edu[20];
                strcpy(edu,".cs.stonybrook.edu");
                printf("the server host is: %s%s\n", (*hptr).h_name, edu);
                break;
            default:
                printf("Unknown address type!!!");
                break;
        }
    }
    
    else{                                                               //the inputted address is the host name;
        if((hptr = gethostbyname (ptr)) == NULL){
            printf("gethostbyname error for host: %s\n", ptr);
        }
         
        switch((*hptr).h_addrtype){
        case AF_INET:
            pptr = (*hptr).h_addr_list;
            inet_ntop( (*hptr).h_addrtype, *pptr, str, sizeof(str));
            printf("IP address: %s\n", str);
            char edu[20];
            strcpy(edu,".cs.stonybrook.edu");
            printf("the server host is: %s%s\n", (*hptr).h_name, edu);
            break;
        default:
            printf("Unknown address type!!!");
            break;
        }
    
    }
    
    
    for(;;){                                                     //infinite loop: querying the user for service: echo and time
        
        char input;
        printf("choose the service you want:\n");
        printf("Press E to get echo service;\n");
        printf("Press T to get time service;\n");
        printf("Press Q to quit the program.\n");
        
        scanf("%s", &input);
        
        //printf("%s", &input);                                   //test the input;
        
        
        int pfd[2];                                               //create a pipe for all services;
        int nread;
        int pid;
        char buff[1024];
        
        
        if(pipe(pfd) == -1){
            printf("pipe failed\n");
            exit(0);
        }
        
        
        if(input == 't'){                                         //user choose time service;
            
            if((pid = fork()) < 0){
                printf("fork failed\n");
            }
            
            if(pid == 0){                                         //child process, write to parent;
                close(pfd[0]);                                    //close the read end of child;
                dup2(pfd[1],3);
                close(pfd[1]);
                
                
                /*
                 do not have to add "\n", because in the parent process: printf("from child: %s\n", buff)
                 there is alreadt a "\n". We don't want another blank line because the fgets() will have
                 to execute twice.
                 */

                
                if( (execlp("xterm", "xterm", "-e", "./timecli", str, (char *) 0   ) ) < 0){
                    printf("xterm error!\n");                     //can print error to the parent
                    exit(0);
                }

                
            }
            else{                                                 //parent process, read from child;
                
                int childended = 0;
                int test = pthread_create(&tid, NULL, handleparentinput, (void *) &childended);
                pthread_detach(tid);
                
                
                close(pfd[1]);                                    //close the write end of parent;
                
                
                while ((nread = read(pfd[0], buff, 1024)) > 0) {  //read(), return # of bytes, 0(eof), -1(error);
                    //printf("%d\n",nread);
                    printf("from child: %s\n", buff);
                }
                close(pfd[0]);                                    //after close, read() return -1;
                int stat;
                int childid = wait(&stat);
                printf("child ended.\n");
            }
            
                     
        }
        
        else if(input == 'e'){                                    //user choose echo service
            if((pid = fork()) < 0){
                printf("fork failed\n");
            }
            
            if(pid == 0){                                         //child process, write to parent;
                close(pfd[0]);                                    //close the read end of child;
                dup2(pfd[1],3);
                close(pfd[1]);

                
                if( (execlp("xterm", "xterm", "-e", "./echocli", str, (char *) 0   ) ) < 0){
                    printf("xterm error!\n");                     //can print error to the parent
                    exit(0);
                }
                
                
            }
            else{                                                 //parent process, read from child;
                
                int childended = 0;
                
                //test if the child has been ended, if ended, the handleparentinput will end accordingly
                int test = pthread_create(&tid, NULL, handleparentinput, (void *) &childended); 
                pthread_detach(tid);
                
                
                
                close(pfd[1]);                                    //close the write end of parent;
                while ((nread = read(pfd[0], buff, 1024)) > 0) {  //read(), return # of bytes, 0(eof), -1(error);
                    //printf("%d\n",nread);
                    printf("from child: %s\n", buff);
                }
                close(pfd[0]);                                    //after close, read() return -1;
                int stat;
                int childid = wait(&stat);
                childended = 1;
                printf("child ended.\n");
                
                    
        
            }
            
            
        }
        
        else if(input == 'q'){                                    //user choose to quit
            printf("Are you sure to quit?\n");
            printf("Press Y for yes, N for no.\n");
            char getchar;
            while (1) {
                scanf("%s", &getchar);
                if(getchar == 'y'){
                    ifquit = 1;
                    break;
                }
                else if (getchar == 'n'){
                    break;
                }
                else {
                    printf("Wrong command!!!Please enter your command again.\n");
                    printf("press Y for yes, N for no.\n");
                }
            }
            
            
        }
        
        else{                                                     //user typed wrong command
            printf("Wrong command!!! Please enter your command again.\n");
        }
        
        
        if(ifquit == 1){
            printf("client exits normally.\n");
            break;
        }
        
    }

    
    
}











