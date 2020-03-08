import estats as stat
import socket
import sys
import os

class Client():
    def __init__(self, configfile):
        config = read(configfile)
        self.id = config['Id']
        self.elems = config['Params']
        self.local_tcp = config['Local-TCP']
        self.server = config['Server']
        self.server_udp = config['Server-UDP']
        self.current_state = stat.NOT_REGISTERED
        self.num_registration_attemps = 0
        super().__init__()

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


def read(filename):
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
                "Error llegint el fitxer de configuració del client, potser no existeix")
            sys.exit()