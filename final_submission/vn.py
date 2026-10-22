import argparse
import select
import socket
import struct
import sys


ON_PORT=5000
MSG_LS=2
CONNECT_MSG=1
MSG_LSP=3
LINK_STATE_TUPLE_SIZE=11
LSP_HEADER_SIZE=8
LSP_NEIGHBOUR_SIZE=5
SEND_INTERVAL=10


def receive_bytes(socket,byte_count):
    data_read=[]
    to_be_read=byte_count
    while to_be_read:
        data=socket.recv(to_be_read)
        if not data:
            raise ConnectionError("TCP connection closed")
        data_read.append(data)
        to_be_read-=len(data)
    return b"".join(data_read)


def  receive_link_state(tcp_socket):
    message_type,size=struct.unpack("!BH",receive_bytes(tcp_socket,3))
    if message_type!=MSG_LS:
        raise ValueError("Not a tcp msg")
    msg=receive_bytes(tcp_socket,size*LINK_STATE_TUPLE_SIZE)
    graph_data=[]
    index=0
    for _ in range(size):
        node=chr(msg[index])
        ip=socket.inet_ntoa(msg[index+1:index+5])
        port=struct.unpack("!H",msg[index+5:index+7])[0]
        cost=struct.unpack("!I",msg[index+7:index+11])[0]
        graph_data.append((node,ip,port,cost))
        index+=LINK_STATE_TUPLE_SIZE
    return graph_data


def encode_ls_packet(sender,sequence_number,costs):
    packet=bytearray(struct.pack("!BBIH",MSG_LSP,ord(sender),sequence_number,len(costs)))
    for neighbour,cost in costs.items():
        packet.extend(struct.pack("!Bi",ord(neighbour),cost))
    return bytes(packet)


def decode_ls_packet(packet):
    if len(packet)<LSP_HEADER_SIZE:
        raise ValueError("LSP len shorter than header")
    msg_type,sender,sequence_number,size=struct.unpack("!BBIH",packet[:LSP_HEADER_SIZE])
    if(msg_type!=MSG_LSP):
        raise ValueError("Msg not an LSP")
    length=LSP_HEADER_SIZE+size*LSP_NEIGHBOUR_SIZE
    if len(packet)!=length:
        raise ValueError("LSP length is not as expected")
    sender=chr(sender)
    edges={}
    index=LSP_HEADER_SIZE
    for _ in range(size):
        neighbour,cost=struct.unpack("!Bi",packet[index:index+LSP_NEIGHBOUR_SIZE])
        costs[chr(neighbour)]=cost  
        index+=LSP_NEIGHBOUR_SIZE
    return sender,sequence_number,costs



    def print_graph(graph,my_id):
        print("\n Graph known at this point at node ",my_id)
        for sender in graph:
            costs=graph[sender]
            data=""
            for node,cost in costs.items():
                data+=("Node---"+ node +"cost-----"+ cost+"\n")
            print("Processed data from ",sender)
            if(data):
                print(data,flush=True)
            else:
                print("No data",flush=True)
            
        


def main():

    parser=argparse.ArgumentParser()
    parser.add_argument("on_ip")
    parser.add_argument("my_ip")
    parser.add_argument("udp_port");
    args=parser.parse_args()
    if len(args)!=3:
        print("Only",len(args),"are provideds")
        return 0

    try:
        my_ip=socket.inet_aton(args.my_ip)
    except OSError:
        raise SystemExit("Invalid my_ip")
    try:
        on_ip=socket.inet_aton(args.on_ip)
    except OSError:
        raise SystemExit("Invalid on_ip")

    if not  1<=args.udp_port<=65535:
        raise SystemExit("UDP port out of range")
    
    udp_socket=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    tcp_socket=socket.socket(socket.AF_INET,socket.SOCK_STREAM)

    try:
        udp_socket.bind(args.my_ip,udp_port)
    except OSError as error:
        print("Could not bind UDP socket",error)
        udp_socket.close()
        tcp_socket.close()
        return 1
    
    try:
        tcp_socket.bind(args.on_ip,ON_PORT)
    except OSError as error:
        print("Could not bind TCP socket",error)
        udp_socket.close()
        tcp_socket.close()
        return 1

    connecting_packet=bytes(CONNECT_MSG)+my_ip+struct.pack("!H",udp_port)
    tcp_socket.sendall(connecting_packet)


    print("Connected to oracle node",flush=True)

    my_id=None
    neighbours={}
    edge_costs={}
    sequence_number=0
    graph={}
    last_sequence={}
    last_sent=0
    

    try:
        while True:
            sockets=select.select([tcp_socket,udp_socket],[],[],1.0)[0]

            for socket in sockets:
                if socket is tcp_socket:
                    my_state=receive_link_state(tcp_socket)
                    try:
                        id=next(t[0] for t in my_state if t[3]==0)
                    except:
                        print("My node is not found in the LSP from ON")
                    new_neighbours={}
                    links={}
                    for node,ip,port,cost in my_state:
                        if node==id:
                            continue
                        new_neighbours[node]=(ip,port)
                        links[node]=cost
                    modified=(id!=my_id or links!=costs or new_neighbours!=neighbours )
                    if(modified):
                        my_id=id
                        costs=links
                        neighbours=new_neighbours
                    print("Node ", my_id, "\n")
                    for n,c in costs.items():
                        print("node:" , n, " cost :",c )
                    print("\n",flush=True)

                    if changed or my_id not in graph:
                        sequence_number+=1
                        graph[my_id]=costs
                        last_sequence[my_id]=sequence_number 
                        curr_state_packet=encode_state(my_id,sequence_number,costs)
                        for neighbour in neighbours.values():
                            udp_socket.sendto(curr_state_packet,neighbour)
                        last_sent=time.monotonic()

                elif socket is udp_socket:
                    received_packet,sender=udp_socket.recvfrom(65535)
                    try:
                        node,sequence_number,costs=decode_ls_packet(packet)
                    except ValueError as error:
                        print("Unable to decode the udp packet")
                        continue
                    
                    if node is None or sender==node:
                        continue
                    previous=last_sequence.get(sender,-1)
                    if sequence_number<=previous:
                        continue
                    last_sequence[sender]=sequence_number
                    graph[sender]=costs
                    print("Done with LSP from",sender,"with sequence number ",sequence_number,flush=True)

                    for node,neighbour in neighbours.items():
                        if neighbour!=sender:
                            udp_socket.sendto(received_packet,sender)
                

            this_time=time.monotonic()
            if my_id is not None and this_time-last_sent>=SEND_INTERVAL:
                sequence_number+=1
                last_sequence[my_id]=sequence_number
                graph[my_id]=costs
                packet=encode_ls_packet(my_id,sequence_number,costs)
                for neighbour in neighbours.items():
                    udp_socket.sendto(packet,neighbour)
                    last_sent=this_time
                
            if my_id is not None :
                print_graph(graph,my_id)

    except(ConnectionError,OSError) as error:
        print("Error:", error)



    finally:
        tcp_socket.close()
        udp_socket.close()
    return 0

if __name__=="_main_":
    raise SystemExit(main())
    

