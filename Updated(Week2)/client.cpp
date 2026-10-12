#include <iostream>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

int main(){
    int sockfd;
    struct sockaddr_in their_addr; 
    sockfd = socket(PF_INET, SOCK_STREAM, 0); 
    if(sockfd==-1)perror("sockfd = -1");
    their_addr.sin_family = AF_INET;
    their_addr.sin_port = htons(1943);  
    their_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); 
    memset(&(their_addr.sin_zero), '\0', 8); 
    if(connect(sockfd,(struct sockaddr* )&their_addr,sizeof(struct sockaddr))==-1)
    {perror("Connection error");
        close(sockfd);
        return 1;
    }
    
   const char* msg="Hello World";
   int s=send(sockfd,msg,strlen(msg),0);
   if(s<strlen(msg))cout<<"Messgae not sent";
   close(sockfd);

}