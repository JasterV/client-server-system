import socket
import sys
import struct
import select
import time
import datetime
import threading
import signal
import os
from protocol import *
from paquets import *
from estats import *
from client_object import Client

# Creem una instancia de la classe Client
# que ens ajudarà a gestionar variables com el estat
# del client i la seva configuració
client = Client('client.cfg')


def unpack_response(fmt, response):
    t = struct.unpack(fmt, response)
    return tuple(x if isinstance(x, int) else x.split(b'\x00')[0].decode() for x in t)


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


def build_info_data(local_tcp, params: dict):
    result = ""
    result += str(local_tcp) + ','
    list(params)
    result += ';'.join(params)
    return result


def get_actual_date():
    date = datetime.datetime.now()
    info = str(date.year) + "-" + str(date.month) + "-" + str(date.day)
    info += ";"
    info += str(date.hour) + ":" + str(date.minute) + ":" + str(date.second)
    return info

# -------------------------------------------------------------- #
# --------------------- FASE DE REGISTRE ----------------------- #
# -------------------------------------------------------------- #


def send_info(sock, server_address, server_response):
    global client
    timeout = t * 2
    # Emmagatzemem les dades d'identificacio
    # del servidor
    client.set_server_credentials(
        server_response[1], server_response[2], server_address[0])
    data = build_info_data(client.local_tcp, client.elems)
    address = (client.server_ip, client.udp_port)
    # Enviem les dades
    sendto(sock, address, REG_INFO, client.id, client.rand_num, data)
    client.current_state = WAIT_ACK_INFO
    (inputs, _, _) = select.select([sock], [], [], timeout)
    # Comprovem que ens hagi arribat resposta del servidor
    if len(inputs) > 0:
        response, server_addr = recvfrom(sock)
        pck = response[0]
        if client.has_state(WAIT_ACK_INFO) and client.check_server_credentials(server_id=response[1],
                                                                               rand_num=response[2],
                                                                               server_ip=server_addr[0]):
            if pck == INFO_ACK:
                return INFO_ACK, response[3]
            elif pck == INFO_NACK:
                return INFO_NACK, -1
    return -1, -1


def send_pdus(sock, server_udp_address):
    global client
    num_sends = 0
    timeout = t
    while True:
        if num_sends >= n:
            time.sleep(u)
            register(sock, server_udp_address)
            break
        if num_sends >= p and timeout < q * t:
            timeout += t
        # Enviem la pdu REG al servidor
        sendto(sock, server_udp_address, REG_REQ, client.id, "00000000\0", "")
        client.current_state = WAIT_ACK_REG
        (inputs, _, _) = select.select([sock], [], [], timeout)
        num_sends += 1
        # Comprovem si hem rebut resposta el servidor
        if len(inputs) > 0:
            # Rebem les dades del servidor
            response, server_addr = recvfrom(sock)
            if not client.has_state(WAIT_ACK_REG) or not server_addr[0] == server_udp_address[0]:
                client.current_state = NOT_REGISTERED
            else:
                pck = response[0]
                if pck == REG_ACK:
                    # ---------- ENVIEM PAQUET INFO I ANALITZEM LA RESPOSTA -------------#
                    client.set_udp_communication_port(int(response[3]))
                    info_response = send_info(sock, server_udp_address,
                                              response)
                    pck_info = info_response[0]
                    if pck_info == INFO_ACK:
                        client.set_tcp_communication_port(
                            int(info_response[1]))
                        client.current_state = REGISTERED
                    elif pck_info == INFO_NACK:
                        client.current_state = NOT_REGISTERED
                        continue
                    else:
                        client.current_state = NOT_REGISTERED
                    # ------------- FI ENVIAMENT PAQUET INFO -------------- #
                elif pck == REG_NACK:
                    client.current_state = NOT_REGISTERED
                    continue
                else:
                    client.current_state = NOT_REGISTERED
        if client.has_state(REGISTERED):
            break
        if client.has_state(NOT_REGISTERED):
            register(sock, server_udp_address)
            break


def register(sock, server_address):
    global client
    client.num_registration_attemps += 1
    if client.num_registration_attemps > o:
        print("No s'ha pogut contactar amb el servidor")
        client.current_state = DISCONNECTED
    else:
        # Comencem el procés de registre
        send_pdus(sock, server_address)


# -------------------------------------------------------------- #
# --------------------- FASE DE COMUNICACIÓ -------------------- #
# ---------------------------PERIÒDICA-------------------------- #
# -------------------------------------------------------------- #


def recv_first_alive(sock):
    global client
    timeout = r * v
    (inputs, _, _) = select.select([sock], [], [], timeout)
    if len(inputs) > 0:
        response, server_addr = recvfrom(sock)
        pck, client_id = response[0], response[-1]
        if client.check_server_credentials(server_id=response[1], rand_num=response[2],
                                           server_ip=server_addr[0]) and client_id == client.id:
            if pck == ALIVE:
                client.current_state = SEND_ALIVE
            else:
                client.current_state = NOT_REGISTERED
        else:
            client.current_state = NOT_REGISTERED
    else:
        client.current_state = NOT_REGISTERED


def recv_alive(sock):
    global client
    alives_not_received = 0
    max_lost_alive = s
    timeout = r * v
    while client.has_state(SEND_ALIVE):
        (inputs, _, _) = select.select([sock], [], [], timeout)
        if len(inputs) > 0:
            response, server_addr = recvfrom(sock)
            pck, client_id = response[0], response[-1]
            if client.check_server_credentials(server_id=response[1], rand_num=response[2],
                                               server_ip=server_addr[0]) and client_id == client.id:
                if pck == ALIVE:
                    alives_not_received = 0
                else:
                    client.current_state = NOT_REGISTERED
            else:
                client.current_state = NOT_REGISTERED
        else:
            alives_not_received += 1
        if alives_not_received >= max_lost_alive:
            client.current_state = NOT_REGISTERED


def send_alive(sock, server_udp_address):
    global client
    timeout = v
    while client.has_state(REGISTERED, SEND_ALIVE):
        sendto(sock, server_udp_address, ALIVE, client.id, client.rand_num, "")
        time.sleep(timeout)


# -------------------------------------------------------------- #
# --------------------- REBUDA DE COMANDES --------------------- #
# -------------------------------------------------------------- #
def help():
    print("\tstat")
    print("\tset <identificador_element> <nou_valor>")
    print("\tsend <identificador_element>")
    print("\tquit")


def run_stat():
    global client
    for key, value in client.elems.items():
        print(f"\t{key} : {value}")


def run_set(cmd):
    if len(cmd) != 3:
        print("\tset <identificador_element> <nou_valor>")
    elif cmd[1] not in client.elems:
        print("\tL'element no existeix")
    else:
        elem_id = cmd[1]
        new_value = cmd[2]
        client.elems[elem_id] = new_value


def run_send(sock, cmd):
    if len(cmd) != 2:
        print("\tsend <identificador_element>")
    elif cmd[1] not in client.elems:
        print("\tL'element no existeix")
    else:
        timeout = m
        elem = cmd[1]
        value = client.elems[elem]
        info = get_actual_date()
        send(sock, SEND_DATA, client.id, client.rand_num, elem, value, info)
        (inputs, _, _) = select.select([sock], [], [], timeout)
        if len(inputs) > 0:
            response = recv(sock)
            pck = response[0]
            client_id = response[-1]
            if client.check_server_credentials(server_id=response[1], rand_num=response[2]) and client_id == client.id:
                if pck == DATA_ACK:
                    print("\tDades acceptades!")
                elif pck == DATA_NACK:
                    print("\tDades no acceptades, proba a reenviarles")
                else:
                    client.current_state = NOT_REGISTERED
            else:
                client.current_state = NOT_REGISTERED


def start_cli():
    global client
    print("\nBenvingut, introdueix 'help' per veure les comandes acceptades.")
    while client.has_state(SEND_ALIVE):
        try:
            cmd = input().strip().split()
            if len(cmd) == 0:
                pass
            elif cmd[0] == 'help':
                help()
            elif cmd[0] == 'stat':
                run_stat()
            elif cmd[0] == 'set':
                run_set(cmd)
            elif cmd[0] == 'send':
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.connect((client.server_ip, client.tcp_port))
                run_send(sock, cmd)
                sock.close()
            elif cmd[0] == 'quit':
                client.current_state = DISCONNECTED
            else:
                print("\tComanda no acceptada.")
        except EOFError:
            os.kill(os.getpid(), signal.SIGINT)


# --------------------------------------------------------------
# --------------------- ESPERA DE CONNEXIONS--------------------
# --------------------------------------------------------------
def set_data(sock, elem, value):
    global client
    if elem[-1] != 'I':
        send(sock, DATA_NACK, client.id, client.rand_num,
             elem, "", "Tipus de l'element incorrecte")
    else:
        client.elems[elem] = value
        print(
            f"=> Valor {value} assignat a l'element {elem} per el servidor")
        send(sock, DATA_ACK, client.id,
             client.rand_num, elem, value, client.id)


def get_data(sock, elem):
    print(
        f"=> Dades de l'element {elem} demanades per el servidor.")
    value = client.elems[elem]
    send(sock, DATA_ACK, client.id,
         client.rand_num, elem, value, client.id)


def wait_server_connections(sock):
    while client.has_state(SEND_ALIVE):
        (inputs, _, _) = select.select([sock], [], [], 0.0)
        if len(inputs) > 0:
            new_sock, addr = sock.accept()
            response = recv(new_sock)
            pck, client_id, elem = response[0], response[-1], response[3]
            if client.check_server_credentials(server_id=response[1], rand_num=response[2]) and client_id == client.id:
                if elem not in client.elems:
                    send(new_sock, DATA_NACK, client.id,
                         client.rand_num, elem, "", "Element no trobat")
                if pck == SET_DATA:
                    value = response[4]
                    set_data(new_sock, elem, value)
                elif pck == GET_DATA:
                    get_data(new_sock, elem)
            else:
                send(new_sock, DATA_REJ, client.id, client.rand_num, elem, "",
                     "Hi ha discrepancies en les dades del servidor i/o del dispositiu")
                client.current_state = NOT_REGISTERED
            new_sock.close()
    sock.close()


# ----------------------------------------------------------------
# ------------------------MAIN BLOCK-----------------------------
# ---------------------------------------------------------------

def run_send_alive(udp_sock, server_address):
    s_alive = threading.Thread(target=send_alive, args=[
                               udp_sock, server_address], daemon=True)
    s_alive.start()


def run_recv_alive(udp_sock):
    r_alive = threading.Thread(target=recv_alive, args=[udp_sock], daemon=True)
    r_alive.start()


def run_cli():
    cli = threading.Thread(target=start_cli, daemon=True)
    cli.start()


def run_connections(sock):
    wait_connections = threading.Thread(
        target=wait_server_connections, args=[sock], daemon=True)
    wait_connections.start()


def sig_handler(signum, frame):
    # Realitzem un exit el qual tanca tots els
    # descriptors de fitxers relacionats amb el procés
    sys.exit(0)


if __name__ == '__main__':
    signal.signal(signal.SIGQUIT, sig_handler)
    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTSTP, sig_handler)
    try:
        server_udp_address = (client.server, client.server_udp)
        udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp_sock.bind(client.local_address())
        while not client.has_state(DISCONNECTED):
            if client.has_state(NOT_REGISTERED):
                print("\nRegistrant disposisitiu al servidor...")
                # Iniciem la fase de registre
                register(udp_sock, server_udp_address)
                if client.has_state(REGISTERED):
                    # Comencem a enviar paquets ALIVE al servidor
                    run_send_alive(udp_sock, server_udp_address)
                    # Esperem el primer paquet alive
                    recv_first_alive(udp_sock)
                if client.has_state(SEND_ALIVE):
                    # Iniciem la rebuda de ALIVEs del servidor
                    run_recv_alive(udp_sock)
                    # Obrim el port tcp local per a les connexions amb el servidor
                    tcp_sock = socket.socket(
                        socket.AF_INET, socket.SOCK_STREAM)
                    tcp_sock.bind(('', client.local_tcp))
                    tcp_sock.listen()
                    # Obrim la consola de comandes
                    run_cli()
                    # Iniciem la espera de connexions amb el servidor
                    run_connections(tcp_sock)
    except OSError as err:
        print(f"OSError: {err}")
    except struct.error as err:
        print(f"Struct Error: {err}")
    except Exception as err:
        print(f"{err}")
    finally:
        # Realitzem un exit el qual tanca tots els
        # descriptors de fitxers relacionats amb el procés
        sys.exit(0)
