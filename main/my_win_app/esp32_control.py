import socket
import threading
import tkinter as tk
from tkinter import messagebox


HOST = "0.0.0.0"
PORT = 5000


class ESP32Control:

    def __init__(self, root):

        self.root = root
        self.root.title("ESP32 Control Panel")
        self.root.geometry("500x430")
        self.root.resizable(False, False)

        self.server = None
        self.client = None

        self.connected = False

        # -------------------------
        # عنوان
        # -------------------------

        title = tk.Label(
            root,
            text="ESP32 CONTROL PANEL",
            font=("Arial", 20, "bold")
        )

        title.pack(pady=15)

        # -------------------------
        # وضعیت اتصال
        # -------------------------

        self.connection_label = tk.Label(
            root,
            text="● Waiting for ESP32...",
            font=("Arial", 13)
        )

        self.connection_label.pack(pady=5)

        # -------------------------
        # وضعیت LED
        # -------------------------

        self.led_label = tk.Label(
            root,
            text="LED: Unknown",
            font=("Arial", 15)
        )

        self.led_label.pack(pady=10)

        # -------------------------
        # تاریخ
        # -------------------------

        self.date_label = tk.Label(
            root,
            text="Date: ---",
            font=("Arial", 13)
        )

        self.date_label.pack()

        # -------------------------
        # ساعت
        # -------------------------

        self.time_label = tk.Label(
            root,
            text="Time: ---",
            font=("Arial", 18, "bold")
        )

        self.time_label.pack(pady=5)

        # -------------------------
        # دکمه ها
        # -------------------------

        button_frame = tk.Frame(root)

        button_frame.pack(pady=20)

        self.led_on_button = tk.Button(
            button_frame,
            text="LED ON",
            width=15,
            height=2,
            command=lambda: self.send_command("LED,1")
        )

        self.led_on_button.grid(
            row=0,
            column=0,
            padx=5,
            pady=5
        )

        self.led_off_button = tk.Button(
            button_frame,
            text="LED OFF",
            width=15,
            height=2,
            command=lambda: self.send_command("LED,0")
        )

        self.led_off_button.grid(
            row=0,
            column=1,
            padx=5,
            pady=5
        )

        self.status_button = tk.Button(
            button_frame,
            text="STATUS",
            width=15,
            height=2,
            command=lambda: self.send_command("STATUS")
        )

        self.status_button.grid(
            row=1,
            column=0,
            padx=5,
            pady=5
        )

        self.time_button = tk.Button(
            button_frame,
            text="TIME",
            width=15,
            height=2,
            command=lambda: self.send_command("TIME")
        )

        self.time_button.grid(
            row=1,
            column=1,
            padx=5,
            pady=5
        )

        self.reset_button = tk.Button(
            button_frame,
            text="RESET ESP32",
            width=32,
            height=2,
            command=self.reset_esp32
        )

        self.reset_button.grid(
            row=2,
            column=0,
            columnspan=2,
            pady=10
        )

        # -------------------------
        # Log
        # -------------------------

        self.log = tk.Text(
            root,
            width=55,
            height=6
        )

        self.log.pack(pady=5)

        # -------------------------
        # شروع TCP Server
        # -------------------------

        threading.Thread(
            target=self.start_server,
            daemon=True
        ).start()

        self.root.protocol(
            "WM_DELETE_WINDOW",
            self.close
        )


    # ==================================================
    # TCP SERVER
    # ==================================================

    def start_server(self):

        try:

            self.server = socket.socket(
                socket.AF_INET,
                socket.SOCK_STREAM
            )

            self.server.setsockopt(
                socket.SOL_SOCKET,
                socket.SO_REUSEADDR,
                1
            )

            self.server.bind(
                (HOST, PORT)
            )

            self.server.listen(1)

            self.write_log(
                "TCP server started"
            )

            self.write_log(
                "Waiting for ESP32..."
            )

            while True:

                client, address = self.server.accept()

                self.client = client
                self.connected = True

                self.root.after(
                    0,
                    self.connection_status,
                    True,
                    address[0]
                )

                self.write_log(
                    f"ESP32 connected: {address[0]}"
                )

                threading.Thread(
                    target=self.receive_data,
                    daemon=True
                ).start()

        except Exception as e:

            self.write_log(
                f"Server error: {e}"
            )


    # ==================================================
    # دریافت اطلاعات ESP32
    # ==================================================

    def receive_data(self):

        buffer = ""

        try:

            while self.connected:

                data = self.client.recv(1024)

                if not data:

                    break

                buffer += data.decode(
                    "utf-8",
                    errors="ignore"
                )

                while "\n" in buffer:

                    line, buffer = buffer.split(
                        "\n",
                        1
                    )

                    line = line.strip()

                    if line:

                        self.process_message(line)

        except Exception as e:

            self.write_log(
                f"Receive error: {e}"
            )

        finally:

            self.connected = False

            try:
                self.client.close()
            except:
                pass

            self.root.after(
                0,
                self.connection_status,
                False,
                ""
            )

            self.write_log(
                "ESP32 disconnected"
            )


    # ==================================================
    # پردازش پیام ESP32
    # ==================================================

    def process_message(self, message):

        self.write_log(
            "RX: " + message
        )

        # -------------------------
        # اتصال
        # -------------------------

        if message == "ESP32_CONNECTED":

            return

        # -------------------------
        # LED
        # -------------------------

        if message.startswith("OK,LED="):

            state = message.split(
                "="
            )[1]

            if state == "1":

                self.root.after(
                    0,
                    self.led_label.config,
                    {"text": "LED: ON"}
                )

            else:

                self.root.after(
                    0,
                    self.led_label.config,
                    {"text": "LED: OFF"}
                )

            return

        # -------------------------
        # STATUS
        # -------------------------

        if message.startswith("STATUS,"):

            self.process_status(
                message
            )

            return

        # -------------------------
        # TIME
        # -------------------------

        if message.startswith("TIME,"):

            parts = message.split(",")

            if len(parts) >= 3:

                time_value = parts[1]
                date_value = parts[2]

                self.root.after(
                    0,
                    self.time_label.config,
                    {"text": f"Time: {time_value}"}
                )

                self.root.after(
                    0,
                    self.date_label.config,
                    {"text": f"Date: {date_value}"}
                )

            return

        # -------------------------
        # RESET
        # -------------------------

        if message == "OK,RESET":

            self.write_log(
                "ESP32 is restarting..."
            )

            return


    # ==================================================
    # پردازش STATUS
    # ==================================================

    def process_status(self, message):

        parts = message.split(",")

        for part in parts:

            if part.startswith("LED="):

                state = part.split("=")[1]

                if state == "1":

                    self.root.after(
                        0,
                        self.led_label.config,
                        {"text": "LED: ON"}
                    )

                else:

                    self.root.after(
                        0,
                        self.led_label.config,
                        {"text": "LED: OFF"}
                    )

            elif part.startswith("TIME="):

                value = part.split("=")[1]

                self.root.after(
                    0,
                    self.time_label.config,
                    {"text": f"Time: {value}"}
                )

            elif part.startswith("DATE="):

                value = part.split("=")[1]

                self.root.after(
                    0,
                    self.date_label.config,
                    {"text": f"Date: {value}"}
                )


    # ==================================================
    # ارسال فرمان
    # ==================================================

    def send_command(self, command):

        if not self.connected:

            messagebox.showwarning(
                "ESP32",
                "ESP32 is not connected!"
            )

            return

        try:

            self.client.sendall(
                (command + "\n").encode(
                    "utf-8"
                )
            )

            self.write_log(
                "TX: " + command
            )

        except Exception as e:

            self.write_log(
                f"Send error: {e}"
            )


    # ==================================================
    # RESET
    # ==================================================

    def reset_esp32(self):

        if not self.connected:

            messagebox.showwarning(
                "ESP32",
                "ESP32 is not connected!"
            )

            return

        answer = messagebox.askyesno(
            "Reset ESP32",
            "Are you sure you want to reset ESP32?"
        )

        if answer:

            self.send_command(
                "RESET"
            )


    # ==================================================
    # وضعیت اتصال
    # ==================================================

    def connection_status(
        self,
        connected,
        ip
    ):

        if connected:

            self.connection_label.config(
                text=f"● ESP32 CONNECTED ({ip})"
            )

        else:

            self.connection_label.config(
                text="● ESP32 DISCONNECTED"
            )


    # ==================================================
    # LOG
    # ==================================================

    def write_log(self, text):

        def update():

            self.log.insert(
                tk.END,
                text + "\n"
            )

            self.log.see(
                tk.END
            )

        self.root.after(
            0,
            update
        )


    # ==================================================
    # خروج
    # ==================================================

    def close(self):

        self.connected = False

        try:

            if self.client:
                self.client.close()

            if self.server:
                self.server.close()

        except:
            pass

        self.root.destroy()


# ======================================================
# MAIN
# ======================================================

root = tk.Tk()

app = ESP32Control(root)

root.mainloop()