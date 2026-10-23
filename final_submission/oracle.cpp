#include<arpa/inet.h>
#include<sys/select.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<unistd.h>
#include<cerrno>
#include<chrono>
#include<cstdint>
#include<cstring>
#include<filesystem>
#include<fstream>
#include<iostream>
#include<sstream>
#include<string>
#include<vector>
#include<algorithm>
using namespace std;

static constexpr uint8_t MSG_CONNECT=1;
static constexpr uint8_t MSG_LINK_STATE=2;
static constexpr int ON_PORT=5000;
static constexpr size_t CONNECT_SIZE=7;
static constexpr size_t TUPLE_SIZE=11;

struct Endpoint{
    bool valid=false;
    string ip;
    uint16_t port=0;
};

struct Client{
    int fd=-1;
    int node=-1;
    vector<uint8_t> rx;
    bool connected_message_received=false;
    Endpoint endpoint;
};

struct Config{
    int n=0;
    vector<vector<int32_t>> cost;
};

static bool parse_config(const string &path, Config &out, string &error){
    ifstream in(path);
    if(!in){
        error="Cannot open config file: " + path + ":(";
        return false;
    }
    vector<int32_t> values;
    string line;
    while(getline(in,line)){
        size_t hash=line.find('#');
        if(hash!=string::npos){
            line.erase(hash);
        }
        stringstream ss(line);
        long long x;
        while(ss>>x){
            if(x<-1 || x>2147483647LL){
                error="Invalid link cost in configuration.";
                return false;
            }
            values.push_back(static_cast<int32_t>(x));
        }
    }
    int n=0;
    while(n<=26 && n*(n-1)/2<static_cast<int>(values.size())) ++n;
    if(n>26 || n*(n-1)/2!=static_cast<int>(values.size()) || n<1){
        error="Config must contain exactly N*(N-1)/2 integer costs for 1<=N<=26";
        return false;
    }
    Config c;
    c.n=n;
    c.cost.assign(n,vector<int32_t>(n,-1));
    for(int i=0;i<n;++i) c.cost[i][i]=0;
    size_t k=0;
    for(int i=0;i<n;++i){
        for(int j=i+1;j<n;++j){
            int32_t v=values[k++];
            if(v==0 || v<-1){
                error="Each link cost must be positive or -1 for no link";
                return false;
            }
            c.cost[i][j]=v;
            c.cost[j][i]=v;
        }
    }
    out=std::move(c);
    return true;
}

static bool send_all(int fd,const uint8_t* data,size_t len){
    size_t sent=0;
    while(sent<len){
        ssize_t n=send(fd,data+sent,len-sent,0);
        if(n<0){
            if(errno==EINTR) continue;
            return false;
        }
        if(n==0) return false;
        sent+=static_cast<size_t>(n);
    }
    return true;
}

static void append_u16(vector<uint8_t> &out,uint16_t x){
    out.push_back(static_cast<uint8_t>((x>>8) & 0xff));
    out.push_back(static_cast<uint8_t>(x & 0xff));
}

static void append_u32(vector<uint8_t> &out, uint32_t x){
    out.push_back(static_cast<uint8_t>((x>>24) & 0xff));
    out.push_back(static_cast<uint8_t>((x>>16) & 0xff));
    out.push_back(static_cast<uint8_t>((x>>8) & 0xff));
    out.push_back(static_cast<uint8_t>(x & 0xff));
}

static uint16_t read_u16(const uint8_t* p){
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));

}

static bool valid_ip4(const string& ip){
    in_addr addr{};
    return inet_pton(AF_INET,ip.c_str(),&addr)==1;
}

static bool send_link_state(Client &recipent, const Config& config, const vector<Client>& clients){
    if(recipent.fd<0 || !recipent.connected_message_received) return false;
    int self=recipent.node;
    vector<int> tuple_nodes;
    for(int j=0;j<config.n;++j){
        if(j!=self && config.cost[self][j]>0) tuple_nodes.push_back(j);

    }
    tuple_nodes.push_back(self);
    if(tuple_nodes.size()>26) return false;
    vector<uint8_t> packet;
    packet.reserve(3+tuple_nodes.size() * TUPLE_SIZE);
    packet.push_back(MSG_LINK_STATE);
    append_u16(packet,static_cast<uint16_t>(tuple_nodes.size()));

    for(int node:tuple_nodes){
        if(node<0 || node>=static_cast<int>(clients.size()) || !clients[node].endpoint.valid){
            cerr<<"Missing endpoint for node "<<char('A'+node)<<"\n";
            return false;
        }

        packet.push_back(static_cast<uint8_t>('A'+node));
        in_addr addr{};
        if(inet_pton(AF_INET,clients[node].endpoint.ip.c_str(),&addr)!=1){
            cerr<<"Invalid stored IPv4 address\n";
            return false;
        }
        const uint8_t *ipbytes=reinterpret_cast<const uint8_t*>(&addr.s_addr);
        packet.insert(packet.end(),ipbytes,ipbytes+4);
        append_u16(packet,clients[node].endpoint.port);
        append_u32(packet,static_cast<uint32_t>(config.cost[self][node]));

    }

    if(!send_all(recipent.fd,packet.data(),packet.size())){
        perror("send LINK_STATE");
        return false;
    }
    std::cout<<"Sent LINK-STATE to node "<<char('A'+self)<<"("<<tuple_nodes.size()<<"tuples)"<<endl;
    return true;
}

static void close_client(Client &c){
    if(c.fd>=0) close(c.fd);
    c.fd=-1;
    c.rx.clear();
}

static bool get_local_ip(string &ip){
    int fd=socket(AF_INET,SOCK_DGRAM,0);
    if(fd<0) return false;
    sockaddr_in remote{};
    remote.sin_family=AF_INET;
    remote.sin_port=htons(53);
    if(inet_pton(AF_INET,"8.8.8.8",&remote.sin_addr)!=1){
        close(fd);
        return false;
    }
    if(connect(fd,reinterpret_cast<sockaddr*>(&remote),sizeof(remote))<0){
        close(fd);
        return false;

    }

    sockaddr_in local{};
    socklen_t local_len=sizeof(local);
    if(getsockname(fd,reinterpret_cast<sockaddr*>(&local),&local_len)<0){
        close(fd);
        return false;

    }

    char buffer[INET_ADDRSTRLEN]{};
    if(inet_ntop(AF_INET,&local.sin_addr,buffer,sizeof(buffer))==nullptr){
        close(fd);
        return false;
    }
    close(fd);
    ip=buffer;
    return !ip.empty();
}

int main(int argc,char* argv[]){
    if(argc!=2){
        cerr<<"Usage: "<<argv[0]<<" <config-file>\n"<<"Example: "<<argv[0]<<" oracle-node-config.txt\n";
        return 1;
    }

    // const string bind_ip=argv[1];
    const string config_path=argv[1];
    
    // if(!valid_ip4(bind_ip)){
    //     cerr<<"Error!"<<"\n";
    //     return 1;
    // }
    string bind_ip;
    if(!get_local_ip(bind_ip)){
        cerr<<"Error!\n";
        return 1;
    }

    Config config;
    string error;
    if(!parse_config(config_path,config,error)){
        cerr<<error<<"\n";
        return 1;
    }
    std::cout<<"Loaded "<<config.n<<" nodes from "<<config_path<<endl;
    
    int listen_fd=socket(AF_INET,SOCK_STREAM,0);
    if(listen_fd<0){
        perror("socket");
        return 1;
    }

    int yes=1;
    if(setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes)) < 0){
        perror("setsockopt");
        close(listen_fd);
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(ON_PORT);
    if(inet_pton(AF_INET,bind_ip.c_str(),&server_addr.sin_addr)!=1){
        cerr<<"Invalid bind IP\n";
        close(listen_fd);
        return 1;
    }
    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }
    if (listen(listen_fd, 32) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    vector<Client> clients(config.n);
    int accepted_count=0;
    bool initial_topology_sent=false;
    auto last_write=filesystem::last_write_time(config_path);

    std::cout<<"Oracle Node listening on "<<bind_ip<<": "<<ON_PORT<<endl;


    std::cout<<"Start VNs in order A,B,C..."<<endl;

    while(true){
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listen_fd,&readfds);
        int maxfd=listen_fd;
        for(const auto& c:clients){
            if(c.fd>=0){
                FD_SET(c.fd,&readfds);
                maxfd=max(maxfd,c.fd);
            }
        }
        timeval timeout{};
        timeout.tv_sec=1;
        timeout.tv_usec=0;
        int ready=select(maxfd+1,&readfds,nullptr,nullptr,&timeout);
        if(ready<0){
            if(errno==EINTR) continue;
            perror("select");
            break;
        }

        if(FD_ISSET(listen_fd,&readfds)){
            sockaddr_in peer{};
            socklen_t peer_len=sizeof(peer);
            int fd=accept(listen_fd,reinterpret_cast<sockaddr*>(&peer),&peer_len);
            if(fd<0){
                if(errno!=EINTR) perror("accept");

            }
            else if(accepted_count>=config.n){
                cerr<<"Extra VNs"<<"\n";
                close(fd);
            }
            else{
                int node=accepted_count++;
                clients[node].fd=fd;
                clients[node].node=node;
                char p[INET_ADDRSTRLEN]{};
                inet_ntop(AF_INET,&peer.sin_addr,p,sizeof(p));
                //print
                std::cout<<"Accepted connection for node "<<char('A'+node)<<" from "<<p<<endl;

            }
        }

        for(auto &c:clients){
            if(c.fd<0 || !FD_ISSET(c.fd,&readfds)) continue;
            uint8_t buf[1024];
            ssize_t n=recv(c.fd,buf,sizeof(buf),0);
            if(n<=0){
                if(n<0) perror("recv");
                //print
                std::cout<<"Connection closed for node "<<char('A'+c.node)<<endl;
                close_client(c);
                continue;
            }
            c.rx.insert(c.rx.end(),buf,buf+n);
            while(!c.connected_message_received && c.rx.size()>=CONNECT_SIZE){
                if(c.rx[0]!=MSG_CONNECT){
                    cerr<<"Invalid message type from node "<<char('A'+c.node)<<"\n";
                    close_client(c);
                    break;
                }
                //ip check //ip check

                string ip=to_string(c.rx[1])+"."+to_string(c.rx[2])+"."+to_string(c.rx[3])+"."+to_string(c.rx[4]);
                uint16_t port=read_u16(c.rx.data()+5);
                c.rx.erase(c.rx.begin(),c.rx.begin()+CONNECT_SIZE);

                if(!valid_ip4(ip) || port==0){
                    cerr<<"Invalid VN endpoint: "<<char('A'+c.node)<<endl;
                    close_client(c);
                    break;
                }
                c.endpoint.valid=true;
                c.endpoint.ip=ip;
                c.endpoint.port=port;
                c.connected_message_received=true;
                //print
                std::cout<<"Connect from "<<char('A'+c.node)<<": "<<ip<<": "<<port<<endl;
                
            }
        }

        bool ready_ever=true;
        for(int i=0;i<config.n;++i){
            if(clients[i].fd<0 || !clients[i].connected_message_received || !clients[i].endpoint.valid){
                ready_ever=false;
                break;
            }
        }
        if(ready_ever && !initial_topology_sent){
            bool ok=true;
            for(auto& c:clients){
                if(!send_link_state(c,config,clients)){
                    ok=false;
                }
            }
            if(ok){
                initial_topology_sent=true;
            }
        }
        try {
            auto now_write = filesystem::last_write_time(config_path);
            if (now_write != last_write) {
                Config updated;
                string parse_error;
                if (!parse_config(config_path, updated, parse_error)) {
                    cerr << "Config update rejected: " << parse_error << "\n";
                } else if (updated.n != config.n) {
                    cerr << "Config update rejected: node count must remain fixed.\n";
                } else {
                    config = std::move(updated);
                    std::cout << "Configuration changed; broadcasting updated LINK-STATE.\n";
                    if (ready_ever) {
                        for (auto& c : clients) {
                            if (c.fd >= 0 && c.connected_message_received)
                                send_link_state(c, config, clients);
                        }
                    }
                }
                last_write = now_write;
            }
        }
        catch (const filesystem::filesystem_error& e) {
            cerr << "Config monitoring error: " << e.what() << "\n";
        }
    }
    for(auto& c:clients) close_client(c);
    close(listen_fd);
    return 0;
}