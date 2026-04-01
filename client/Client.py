import tkinter as tk
from tkinter import ttk
import subprocess
import threading
import os

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
API_EXE = os.path.join(BASE_DIR, "..", "build", "clientAPI.exe")
API_EXE = os.path.abspath(API_EXE)


##API_EXE = "clientAPI.exe"

class ClientAPI:
    def __init__(self):
        self.proc = subprocess.Popen(
            [API_EXE],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1
        )

    def send(self, msg):
        self.proc.stdin.write(msg + "\n")
        self.proc.stdin.flush()

    def read_loop(self, callback):
        while True:
            line = self.proc.stdout.readline()
            if not line:
                break
            callback(line.strip())

api = ClientAPI()

def on_response(text):
    output_box.config(state="normal")
    output_box.insert("end", text + "\n")
    output_box.config(state="disabled")


def send_cmd(cmd):
    api.send(cmd)

root = tk.Tk()
root.title("ClientApp UI")

ttk.Button(root, text="Emergency", command=lambda: send_cmd("EMERGENCY")).pack(fill="x")    ## for now these are all hardcoded, but eventually we can add input fields for the user to specify args
ttk.Button(root, text="Weather Toronto", command=lambda: send_cmd("WEATHER YYZ")).pack(fill="x")
ttk.Button(root, text="Flight AC123", command=lambda: send_cmd("FLIGHT AC123")).pack(fill="x")
ttk.Button(root, text="Login Ryan", command=lambda: send_cmd("LOGIN RHackbart91 Gamma789")).pack(fill="x")

output_box = tk.Text(root, height=15, width=60, state="disabled")
output_box.pack(pady=10)

threading.Thread(target=api.read_loop, args=(on_response,), daemon=True).start()

root.mainloop()
