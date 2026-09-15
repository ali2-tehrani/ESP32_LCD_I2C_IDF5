import socket

HOST = "0.0.0.0"
PORT = 5000

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

server.setsockopt(
    socket.SOL_SOCKET,
    socket.SO_REUSEADDR,
    1
)

server.bind((HOST, PORT))

server.listen(1)

print("================================")
print("TCP SERVER STARTED")
print("Listening on port:", PORT)
print("Waiting for ESP32...")
print("================================")

while True:

    client, address = server.accept()

    print()
    print("ESP32 connected!")
    print("IP:", address[0])
    print("Port:", address[1])

    try:

        while True:

            data = client.recv(1024)

            if not data:
                print("ESP32 disconnected")
                break

            message = data.decode(
                "utf-8",
                errors="ignore"
            ).strip()

            print("RX:", message)

    except Exception as e:

        print("Connection error:", e)

    finally:

        client.close()