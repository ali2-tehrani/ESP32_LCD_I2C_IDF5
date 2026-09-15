import socket

HOST = "0.0.0.0"
PORT = 5000

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

server.bind((HOST, PORT))
server.listen(1)

print("TCP SERVER STARTED")
print("Waiting for ESP32...")

client, address = server.accept()

print("ESP32 connected!")
print("IP:", address[0])
print("Port:", address[1])

while True:
    command = input("Command: ")

    if command.lower() == "exit":
        break

    client.sendall((command + "\n").encode("utf-8"))

    data = client.recv(1024)

    if not data:
        print("ESP32 disconnected")
        break

    print("RX:", data.decode("utf-8", errors="ignore"))

client.close()
server.close()