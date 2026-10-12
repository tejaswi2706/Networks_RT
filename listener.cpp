#include  <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstdio>
#include<iostream>
using namespace std;

int main(){
    int sockfd, new_fd;
     struct sockaddr_in my_addr;
    struct sockaddr_in client_addr{};
    int sin_size; 
    sockfd = socket(PF_INET, SOCK_STREAM, 0); 
    if(sockfd==-1)perror("sockfd = -1");
   my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(1943);  
    my_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); 
    memset(&(my_addr.sin_zero), '\0', 8); 
    if(bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr))==-1)perror("Binding unsuccessful"); 
    listen(sockfd, 10); 
    
    sin_size = sizeof(struct sockaddr_in); 
    new_fd = accept(sockfd, (struct sockaddr *)&client_addr, &sin_size); 

    if(new_fd<0){
        perror("Accept failed");
        close(sockfd);
    }


    char buffer[100];
    int n=recv(new_fd,buffer,sizeof(buffer)-1,0);
    if(n<=0){
        perror("Not read");
        
    }
    else {
        buffer[n]='\0';
        cout<<"buffer = "<<buffer<<"\n";

    }
    close (new_fd);
    close(sockfd);
}