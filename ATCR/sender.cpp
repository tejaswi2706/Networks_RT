#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstdio>


int main(){

    int sockfd;
    sockfd=socket(AF_INET,SOCK_DGRAM,0);
    // printf("%d\n",sockfd);
    struct sockaddr_in my_addr;
    my_addr.sin_family=AF_INET;
    my_addr.sin_port=htons(1943);
    my_addr.sin_addr.s_addr=inet_addr("127.0.0.1")
    memset(&(my_addr.sin_zero),'\0',8);
    bind(sockfd,(struct sockaddr*)&my_addr,sizeof(struct sockaddr));
    char* msg="Hello!";
    

    sendto(sockfd,msg,strlen(msg),0,(struct sockaddr* )&my_addr,sizeof(myaddr));
    close(sockfd);
    return 0;
    // int s=sendto(sockfd,)
    
}