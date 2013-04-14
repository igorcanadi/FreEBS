import socket

host = ''
port = 9000
backlog = 5
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.bind((host, port))
s.listen(backlog)

while True:
    client, address = s.accept()
    data = client.recv(16 * 1024)
    if data:
        client.send('ACK\n')
    client.close()
