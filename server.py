#!/usr/bin/env python3

import random
import sys
import datetime
import time
import socket
import select
import threading
import struct

debug_option = False
software_file = "server.cfg"
authorized_teams = "equips.dat"
authorized_teams_ids_macs = []
server_thread = None
server = None
all_clients = []
registered_clients = []
alive_clients = []
UDP_PDU_BYTES = "B7s13s7s50s"
TCP_PDU_BYTES = "B7s13s7s150s"
TCP_BYTES = 178
R = 2
J = 2
W = 3
DELAY_TIME = 0.5 # symbolic time needed to effectively execute verify_alive_inf and verify_first_alive functions that
                  # run on threads. (Sync time). 

class Server:
    def __init__(self, id, mac, UDP_port, TCP_port):
        self.id = id
        self.mac = mac
        self.UDP_port = UDP_port
        self.TCP_port = TCP_port

# Objects of this class will represent the clients connected to the server and their status.
class Client:
    global alive_clients
    def __init__(self, id, mac):
        self.id = id
        self.mac = mac
        self.rand = None
        self.state = "DISCONNECTED"
        self.first_pack = True
        self.first_alive = True
        self.time_since_last_alive = None
        self.file = None
        self.operating = False # control if client is doing send/get op.

    def update(self, state):
        self.state = state
        print(f"{get_time()}: MSG => Client: {self.id} current state: {state}")
    
    # Check if client has lost periodic communication.
    def verify_alive_inf(self):
        if self.state == "ALIVE":
           elapsed_time = time.time() - self.time_since_last_alive
           if elapsed_time > (R * 3 + DELAY_TIME):
                print(f"{get_time()}: MSG => Client: {self.id} reached 3 consecutive alives without response.")
                self.update("DISCONNECTED")
                alive_clients.remove(self)
                self.time_since_last_alive = None
                self.first_alive = True
    
    # Check if client sends first alive_inf in J * R seconds.
    def verify_first_alive(self):
        if self.state == "REGISTERED":
            current_time = time.time()
            while time.time() - current_time < (J * R + DELAY_TIME):
                if self.state == "ALIVE":
                    return
            print(f"{get_time()}: MSG => Client: {self.id} didn't sent first alive package before {J * R} seconds.")
            self.update("DISCONNECTED")

def handle_parameters():
    global software_file, authorized_teams, debug_option
    if '-c' in sys.argv:
        index = sys.argv.index('-c')
        if is_valid_software_file(sys.argv[index + 1]):
            software_file = sys.argv[index + 1]
    if '-u' in sys.argv:
        index = sys.argv.index('-u')
        if is_valid_teams_file(sys.argv[index + 1]):
            authorized_teams = sys.argv[index + 1]
    debug_option = True if '-d' in sys.argv else False

def is_valid_software_file(file):
    if file.startswith("server") and file.endswith(".cfg"): return True
    return False

def is_valid_teams_file(file):
    if file.startswith("equips") and file.endswith(".dat"): return True
    return False

def fill_authorized_teams():
    global authorized_teams_ids_macs
    with open(authorized_teams) as file:
        for line in file:
            text = line.strip().split()
            id = text[0]
            mac = text[1]
            authorized_teams_ids_macs.append([id, mac])

def authorized(team):
    return team in authorized_teams_ids_macs

def registered(client):
    return client in registered_clients

def read_server():
    global server
    with open(software_file, 'r') as file:
        for line in file:
            text = line.strip().split()
            if text[0] == "Id":
                id = text[1]
            elif text[0] == "MAC":
                mac = text[1]
            elif text[0] == "UDP-port":
                udp_port = int(text[1])
            elif text[0] == "TCP-port":
                tcp_port = int(text[1])
    server = Server(id, mac, udp_port, tcp_port)

def get_time():
    return datetime.datetime.now().strftime("%H:%M:%S")

def wait_input_commands():
    if debug_option:
        print("CLI commands available: ")
        print("1) list to see all authorized teams.")
        print("2) quit to end the server.")
    while True:
        command = get_input_command()
        if command == "list":
            show_clients()
        elif command == "quit":
            end_server()
        else:
            print(f"{get_time()}: MSG => {command} is an invalid command!")

def get_input_command():
    command = input()
    return command.strip()

def find_client_by_id(id):
    for client in all_clients:
        if client.id == id:
            return client
    return None

def show_clients():
    print("--ID--" + " ------IP-------" + " -----MAC----" + " --RAND-" + " ----STATE---")
    with open(authorized_teams, 'r') as file:
        for line in file:
            id = line[:6]
            client = find_client_by_id(id)
            mac = line[7:19]
            ip = "-" if client is None else "127.0.0.1"
            rand = "-" if client is None else client.rand
            state = "DISCONNECTED" if client is None else client.state
            print(f"{id} {ip.center(15)} {mac} {rand.center(7)} {state}")

def end_server():
    if debug_option:
        print(f"{get_time()}: MSG => Finalizing server...")
    time.sleep(1)
    sys.exit()

# Building packages
def sendACK_pack(random, id):
    data = id + ".cfg"
    return struct.pack(TCP_PDU_BYTES, 0x24, server.id.encode("utf-8"), server.mac.encode("utf-8"),
                       random.encode("utf-8"), data.encode("utf-8"))

def sendNACK_pack(nack_arg):
    return struct.pack(TCP_PDU_BYTES, 0x26, "".encode("utf-8"), "000000000000".encode("utf-8"),
                       "000000".encode("utf-8"), nack_arg.encode("utf-8"))

def sendREJ_pack(rej_arg):
    return struct.pack(TCP_PDU_BYTES, 0x28, "".encode("utf-8"), "000000000000".encode("utf-8"),
                       "000000".encode("utf-8"), rej_arg.encode("utf-8")) 

def getACK_pack(random, id):
    data = id + ".cfg"
    return struct.pack(TCP_PDU_BYTES, 0x34, server.id.encode("utf-8"), server.mac.encode("utf-8"),
                       random.encode("utf-8"), data.encode("utf-8"))

def getNACK_pack(nack_arg):
    return struct.pack(TCP_PDU_BYTES, 0x36, "".encode("utf-8"), "000000000000".encode("utf-8"),
                       "000000".encode("utf-8"), nack_arg.encode("utf-8"))

def getREJ_pack(rej_arg):
    return struct.pack(TCP_PDU_BYTES, 0x38, "".encode("utf-8"), "000000000000".encode("utf-8"),
                       "000000".encode("utf-8"), rej_arg.encode("utf-8"))

def cfg_file_to_server(id, mac, rand, sock, ip):
    team = [id, mac]
    client = find_client_by_id(id)

    if authorized(team) and client.rand == rand and registered(client) and not client.operating:
        pack = sendACK_pack(rand, id)
        sock.sendall(pack)
        receive_file(client, sock)
        return
    elif rand != client.rand:
        if debug_option:
            print(f"{get_time()}: MSG => Received random number: {rand} but expected {client.rand}.")
        pack = sendNACK_pack("Wrong random number.") 
        sock.sendall(pack)
        sock.close()
    elif ip != "127.0.0.1":
        if debug_option:
            print(f"{get_time()}: MSG => ip: {ip} but expected 127.0.0.1 .")
        pack = sendNACK_pack("Wrong ip address.")
        sock.sendall(pack)
        sock.close()
    elif client.operating:
        if debug_option:
            print(f"{get_time()}: MSG => Client {client.id} is doing a send or get file task.")
        pack = sendNACK_pack("send/get operation in course")
        sock.sendall(pack)
        sock.close()
    elif not authorized(team):
        if debug_option:
            print(f"{get_time()}: MSG => Client {id} with MAC {mac} not registered.")
        pack = sendREJ_pack("Client not authorized.")
        sock.sendall(pack)
        sock.close()
    elif not registered(client):
        if debug_option:
            print(f"{get_time()}: MSG => Client {client.id} not registered.")
        pack = sendREJ_pack("Client not registered.")
        sock.sendall(pack)
        sock.close()

def receive_file(client, sock):
    client.operating = True
    if client.file is None:
        f_name = client.id + ".cfg"
        with open(f_name, 'w') as cfile:
            client.file = f_name
            cfile.close()
    while True:
        i, o, e = select.select([sock], [], [], W) # Check if there's data in the TCP socket in less than W seconds.
        if sock in i:
            data = sock.recv(TCP_BYTES)
            type, id, mac, random, line = struct.unpack(TCP_PDU_BYTES, data)
            if get_PDU_type(type) == "SEND_DATA":
                trueline = line[:line.find(b'\n')]
                addline = trueline.decode("utf-8")
                with open(client.file, 'a') as cfile:
                    cfile.write(addline + '\n')   
            elif get_PDU_type(type) == "SEND_END":
                if debug_option:
                    print(f"{get_time()}: MSG => Reception of configuration file finalized.")
                sock.close()
                cfile.close()
                client.operating = False
                break
        else:
            if debug_option:
                print(f"{get_time()}: MSG => Bad communications. Reached {W} seconds with no response. Closing socket.")
            client.operating = False
            sock.close()
            break

# Building packages
def getDATA_pack(rand, line):
    return struct.pack(TCP_PDU_BYTES, 0x32, server.id.encode("utf-8"), server.mac.encode("utf-8"), rand.encode("utf-8"),
                       line.encode("utf-8"))

def getEND_pack(rand):
    return struct.pack(TCP_PDU_BYTES, 0x3A, server.id.encode("utf-8"), server.mac.encode("utf-8"), rand.encode("utf-8"),
                       "".encode("utf-8"))

def send_file(client, sock):
    client.operating = True
    try:
        with open(client.file, 'r') as cfile:
            for line in cfile:
                pack = getDATA_pack(client.rand, line)
                sock.sendall(pack)
            pack = getEND_pack(client.rand)
            sock.sendall(pack)
            if debug_option:
                print(f"{get_time()}: MSG => Sending of configuration file finalized.")
            cfile.close()
            sock.close()
            client.operating = False
    except FileNotFoundError:
        print(f"{get_time()}: MSG => Could not open the file.")
        client.operating = False
        sock.close()

def cfg_file_to_client(id, mac, rand, sock, ip):
    team = [id, mac]
    client = find_client_by_id(id)

    if authorized(team) and client.rand == rand and registered(client) and not client.operating:
        pack = getACK_pack(rand, id)
        sock.sendall(pack)
        send_file(client, sock) 
        return
    elif rand != client.rand:
        if debug_option:
            print(f"{get_time()}: MSG => Received random number: {rand} but expected {client.rand}.")
        pack = getNACK_pack("Wrong random number.")
        sock.sendall(pack)
        sock.close()
    elif client.operating:
        if debug_option:
            print(f"{get_time()}: MSG => Client {client.id} is doing a send or get file task.")
        pack = getNACK_pack("send/get operation in course")
        sock.sendall(pack)
        sock.close()
    elif ip != "127.0.0.1":
        if debug_option:
            print(f"{get_time()}: MSG => ip: {ip} but expected 127.0.0.1 .")
        pack = getNACK_pack("Wrong ip address.")
        sock.sendall(pack)
        sock.close()
    elif not authorized(team):
        if debug_option:
            print(f"{get_time()}: MSG => Client {id} with MAC {mac} not registered.")
        pack = getREJ_pack("Client not authorized.")
        sock.sendall(pack)
        sock.close()
    elif not registered(client):
        if debug_option:
            print(f"{get_time()}: MSG => Client {client.id} not registered.")
        pack = getREJ_pack("Client not registered.")
        sock.sendall(pack)
        sock.close()

def attend_tcp_client(client_sock, ip):
    try:
        data = client_sock.recv(TCP_BYTES)
        type, id, mac, rand, rcvd_data = struct.unpack(TCP_PDU_BYTES, data)
        id = id.decode("utf-8").rstrip('\0')
        mac = mac.decode("utf-8").rstrip('\0')
        rand = rand.decode("utf-8").rstrip('\0')
    
        if get_PDU_type(type) == "SEND_FILE":
            cfg_file_to_server(id, mac, rand, client_sock, ip)
        elif get_PDU_type(type) == "GET_FILE":
            cfg_file_to_client(id, mac, rand, client_sock, ip)
        else:
            if debug_option:
                print(f"{get_time()}: MSG => Unexpected package type or error.")
            client_sock.close()
    except (UnicodeDecodeError, AttributeError):
        print(f"{get_time()}: MSG => Can't decode received package. Client sent invalid UDP format.")

# This function manages the TCP connections. Creates a socket, binds, listens an accepts connections infinitely,
# if receives connection we manage it creating a thread.
def handle_tcp():
    try:
        tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        if debug_option:
            print(f"{get_time()}: MSG => TCP channel established.")
        tcp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1) # sock can reuse the address.
        tcp_sock.bind(("localhost", server.TCP_port))
        tcp_sock.listen(3) # TCP clients queue. Increase or decrease value depending on needs.
    except socket.error:
        print(f"{get_time()}: MSG => Couldn't create the socket.")
    try:
        while True:
            new_sock, (ip, port) = tcp_sock.accept()
            tcp_client = threading.Thread(target=attend_tcp_client, args=(new_sock, ip))
            tcp_client.daemon = True
            tcp_client.start()
    finally:
        tcp_sock.close()

# Building packages.
def regACK_pack(random):
    return struct.pack(UDP_PDU_BYTES, 0x02, server.id.encode("utf-8"), server.mac.encode("utf-8"),
                       random.encode("utf-8"), str(server.TCP_port).encode("utf-8")) # possible str a client.rand

def regNACK_pack(nack_arg):
    return struct.pack(UDP_PDU_BYTES, 0x04, "000000".encode("utf-8"), "000000000000".encode("utf-8"),
                       "000000".encode("utf-8"), nack_arg.encode("utf-8"))

def regREJ_pack(rej_arg):
    return struct.pack(UDP_PDU_BYTES, 0x06, "000000".encode("utf-8"), "000000000000".encode("utf-8"),
                       "000000".encode("utf-8"), rej_arg.encode("utf-8"))


def aliveACK_pack(random):
    return struct.pack(UDP_PDU_BYTES, 0x12, server.id.encode("utf-8"), server.mac.encode("utf-8"),
                       random.encode("utf-8"), "".encode("utf-8")) # possible str a client.rand

def aliveNACK_pack(nack_arg):
    return struct.pack(UDP_PDU_BYTES, 0x14, "000000".encode("utf-8"), "000000000000".encode("utf-8"),
                       "000000".encode("utf-8"), nack_arg.encode("utf-8"))

def aliveREJ_pack(rej_arg):
    return struct.pack(UDP_PDU_BYTES, 0x16, "000000".encode("utf-8"), "000000000000".encode("utf-8"),
                       "000000".encode("utf-8"), rej_arg.encode("utf-8"))

# Executed by a thread while server is executing, checks for the clients that are registered and it status is "REGISTERED" if
# they send the first_alive in J * R seconds or not.
def checkfirst():
    while True:
        for client in registered_clients:
            client.verify_first_alive()

# Executed by a thread while server is executing, checks for the clients that reached "ALIVE" state if they don't surpass
# 6 seconds without sending at least one alive_inf. 
def alivesloop():
    while True:
        for client in alive_clients:
            client.verify_alive_inf()

def alives(id, mac, ip, port, rand, client, sock):
    global alive_clients
    if client.first_alive:
        client.update("ALIVE")
    client.first_alive = False
    
    if client not in alive_clients: alive_clients.append(client)
    client.time_since_last_alive = time.time()
    
    if id == client.id and mac == client.mac and ip == "127.0.0.1" and client.rand == rand:
        pack = aliveACK_pack(client.rand)
        sock.sendto(pack, (ip, port))
    elif not authorized([id, mac]):
        if debug_option:
            print(f"{get_time()}: MSG => Client: {id} with MAC: {mac} not in authorized teams.")
        pack = aliveREJ_pack("Client not authorized.")
        sock.sendto(pack, (ip, port))
    elif not registered(client):
        if debug_option:
            print(f"{get_time()}: MSG => Client: {client.id} didn't achieve REGISTERED status.")
        pack = aliveREJ_pack("Client not registered.")
        sock.sendto(pack, (ip, port))
    elif ip != "127.0.0.1":
        if debug_option:
            print(f"{get_time()}: MSG => ip: {ip} but expected 127.0.0.1 .")
        pack = aliveNACK_pack("Wrong ip address.")
        sock.sendto(pack, (ip, port))
    elif rand != client.rand:
        if debug_option:
            print(f"{get_time()}: MSG => Received random number: {rand} but expected {client.rand}.")
        pack = aliveNACK_pack("Wrong random number.")
        sock.sendto(pack, (ip, port))

# Main UDP function
def handle_udp(sock):
    global registered_clients, all_clients
    UDP_BYTES = 78
    try:
        while True:
            data, (ip, port) = sock.recvfrom(UDP_BYTES)
            type, id, mac, rand, rcvd_data = struct.unpack(UDP_PDU_BYTES, data)
            id = id.decode("utf-8").rstrip('\0')
            mac = mac.decode("utf-8").rstrip('\0')
            rand = rand.decode("utf-8").rstrip('\0')

            team = [id, mac]
    
            client = find_client_by_id(id)
            if not registered(client):
                client = Client(id, mac)
                client.rand = rand
                all_clients.append(client)
                client.update("WAIT_REG_RESPONSE")
                
            if client.state == "REGISTERED" or client.state == "ALIVE" and authorized(team):
                alives(id, mac, ip, port, rand, client, sock)

            elif (client.state == "REGISTERED" or client.state == "ALIVE") and \
                    get_PDU_type(type) == "REGISTER_REQ" and authorized(team):
                pack = regACK_pack(client.rand)
                sock.sendto(pack, (ip, port))
            elif authorized(team) and get_PDU_type(type) == "REGISTER_REQ" and \
                    (client.state == "WAIT_REG_RESPONSE" or client.state == "DISCONNECTED"):
                client.update("WAIT_DB_CHECK")
                client.first_pack = False
                if not registered(client):
                    client.rand = str(random.randint(100000, 999999))
                    registered_clients.append(client)
                pack = regACK_pack(client.rand)
                sock.sendto(pack, (ip, port))
                client.update("REGISTERED")
            elif not authorized(team):
                if debug_option:
                    print(f"{get_time()}: MSG => Team {team} not in authorized teams list!")
                pack = regREJ_pack("Client not authorized.")
                sock.sendto(pack, (ip, port))
                client.update("DISCONNECTED")
            elif (client.rand != "000000" and client.first_pack) or (ip != "127.0.0.1" and not client.first_pack):
                if debug_option:
                    print(f"{get_time()}: MSG => Client {id} Register request not accepted.")
                pack = regNACK_pack("First pack doesn't contain random with all values to 0.") if client.rand != "000000" \
                                                            and client.first_pack else regNACK_pack("Invalid IP address.")
                sock.sendto(pack, (ip, port))
    finally:
        sock.close()
        
# Creates a thread to manage tcp connections.
def tcp():
    time.sleep(0.1)
    tcp_tasks = threading.Thread(target=handle_tcp)
    tcp_tasks.daemon = True
    tcp_tasks.start()

# Creates two thread for the previously commented functions and creates a socket to receive UDP data.
def udp():

    concurrency = threading.Thread(target=alivesloop)
    concurrency.daemon = True
    concurrency.start()

    checkfirst_thread = threading.Thread(target=checkfirst)
    checkfirst_thread.daemon = True
    checkfirst_thread.start()

    try:
        udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp_sock.bind(("localhost", server.UDP_port))
        if debug_option:
            print(f"{get_time()}: MSG => UDP channel established.")
        handle_udp(udp_sock)
    except socket.error:
        print(f"{get_time()}: MSG => Couldn't create the socket.")

def make_channels():
    tcp()
    udp()

# Return the string associated given a hex value.
def get_PDU_type(value):
    if value == 0x00:
        return "REGISTER_REQ"
    elif value == 0x0F:
        return "ERROR"
    elif value == 0x10:
        return "ALIVE_INF"
    elif value == 0x20:
        return "SEND_FILE"
    elif value == 0x22:
        return "SEND_DATA"
    elif value == 0x2A:
        return "SEND_END"
    elif value == 0x30:
        return "GET_FILE"
    else:
        return None

# Creates a thread for manage the udp and tcp while main process is waiting for input commands.
def make_server():
    global server_thread
    server_thread = threading.Thread(target=make_channels)
    server_thread.daemon = True
    server_thread.start()

def main():
    try:
        handle_parameters()
        fill_authorized_teams()
        read_server()
        make_server()
        wait_input_commands()
    except KeyboardInterrupt:
        print(f"{get_time()}: MSG => Exiting due to KeyboardInterrupt (Ctrl C).")    


if __name__ == '__main__':
    main()
