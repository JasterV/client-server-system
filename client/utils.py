import socket
import sys
import struct
import select
import time
import datetime
import threading
import signal
import os


# Paquets de la fase de registre
REG_REQ = 0x00
REG_INFO = 0x01
REG_ACK = 0x02
INFO_ACK = 0x03
REG_NACK = 0x04
INFO_NACK = 0x05
REG_REJ = 0x06

# Paquets per a la comunicació periòdica
ALIVE = 0x10
ALIVE_REJ = 0x11

# Paquets per a la transferència de dades amb el servidor
SEND_DATA = 0x20
SET_DATA = 0x21
GET_DATA = 0x22
DATA_ACK = 0x23
DATA_NACK = 0x24
DATA_REJ = 0x25

# Els possibles estats que pot tenir un client
DISCONNECTED = 0xa0
NOT_REGISTERED = 0xa1
WAIT_ACK_REG = 0xa2
WAIT_INFO = 0xa3
WAIT_ACK_INFO = 0xa4
REGISTERED = 0xa5
SEND_ALIVE = 0xa6


# Valors per a les proves de protocol de registre
t = 1.0
u = 2.0
n = 7.0
o = 3.0
p = 3.0
q = 3.0
v = 2.0
r = 2.0
s = 3.0
m = 3.0

# -----------------------------------
# ----------DEBUG UTILITIES----------
# -----------------------------------


class Logger:
    def __init__(self):
        self.debug_mode = False

    def turn_debug_on(self):
        self.debug_mode = True

    def debug_print(self, message):
        now = datetime.datetime.now()
        if self.debug_mode:
            print("{0}:{1}:{2} => {3}".format(
                now.hour, now.minute, now.second, message))

# --------------------------------------------
# -------------SOCKETS UTILITIES--------------
# --------------------------------------------


def unpack_response(fmt, response):
    try:
        t = struct.unpack(fmt, response)
        return tuple(x if isinstance(x, int) else x.split(b'\x00')[0].decode() for x in t)
    except:
        # Considerem un error en el desenpaquetament si
        # el servidor ens ha enviat un paquet mal format i
        # retornem null
        return None


def recvfrom(sock):
    response, server_addr = sock.recvfrom(84)
    return unpack_response('!B13s9s61s', response), server_addr


def sendto(sock, address, pack_type, client_id, rand_num, info):
    data = struct.pack('!B13s9s61s', pack_type, client_id.encode(),
                       rand_num.encode(), info.encode())
    sock.sendto(data, address)


def recv(sock):
    response = sock.recv(127)
    return unpack_response('!B13s9s8s16s80s', response)


def send(sock, pack_type, client_id, rand_num, elem_name, elem_value, info):
    data = struct.pack('!B13s9s8s16s80s', pack_type, client_id.encode(), rand_num.encode(
    ), elem_name.encode(), elem_value.encode(), info.encode())
    sock.send(data)

# -------------------------------------
# ------------CLIENT CLASS-------------
# -------------------------------------


class Client():
    """Representa l'estat d'un client amb la seva configuració
    """

    def __init__(self, configfile):
        config = read_cfg(configfile)
        self.id = config['Id']
        self.elems = config['Params']
        self.local_tcp = config['Local-TCP']
        self.server = config['Server']
        self.server_udp = config['Server-UDP']
        self.current_state = NOT_REGISTERED
        self.register_attemps = 0

    def has_state(self, *states):
        for state in states:
            if self.current_state == state:
                return True
        return False

    def set_udp_communication_port(self, udp_port):
        self.udp_port = udp_port

    def set_tcp_communication_port(self, tcp_port):
        self.tcp_port = tcp_port

    def set_server_credentials(self, server_id, rand_num, server_ip):
        self.server_id = server_id
        self.rand_num = rand_num
        self.server_ip = server_ip

    def check_server_credentials(self, server_id=None, rand_num=None, server_ip=None):
        if server_id is not None:
            if server_id != self.server_id:
                return False
        if rand_num is not None:
            if rand_num != self.rand_num:
                return False
        if server_ip is not None:
            if server_ip != self.server_ip:
                return False
        return True

    def local_address(self, port=0):
        return ('', port)


def read_cfg(filename):
    """Llegeix el fitxer de configuració del client
    i retorna un diccionari <String, dynamic> amb les dades llegides
    """
    try:
        cfg = open(filename)
        config = dict()
        for line in cfg:
            items = line.strip().replace('=', ' ').split()
            if len(items) == 2:
                key, value = items
                config[key] = value

        t = config['Params'].strip().split(';')
        config['Params'] = dict((key, "") for key in t)
        config['Local-TCP'] = int(config['Local-TCP'])
        config['Server-UDP'] = int(config['Server-UDP'])
        config['Server'] = socket.gethostbyname(config['Server'])
        return config
    except FileNotFoundError:
        print(
            f"No s'ha trobat el fitxer {filename}")
        sys.exit()
