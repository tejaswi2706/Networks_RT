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
    my_addr.sin_addr.s_addr=inet_addr("127.0.0.1");
    memset(&(my_addr.sin_zero),'\0',8);

    bind(sockfd,(stuct sockaddr*)&my_addr,sizeof(my_addr));

    char buffer[100];
    struct sockaddr_in src;
    recvfrom(sockfd,buffer,sizeof(buffer)-1,0,(struct sockaddr*)&src ,&sizeof(src));
    close(sockfd);
    return 0;


    
}