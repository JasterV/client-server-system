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
