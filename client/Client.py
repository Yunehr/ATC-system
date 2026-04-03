import tkinter as tk
from tkinter import ttk
import subprocess
import threading
import os
from pdfViewer import PDFViewer

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
API_EXE = os.path.join(BASE_DIR, "..", "build", "clientAPI.exe")
API_EXE = os.path.abspath(API_EXE)

download_status = {
    "ready": False,
    "filename": None,
    "error": None
}

class ClientAPI:
    def __init__(self):
        self.proc = subprocess.Popen(
            [API_EXE],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1, 
            cwd=os.path.dirname(API_EXE) # <-- set working directory to where the exe is
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
    #Login ACK/NACK
    if text.startswith("Authentication Successful"):
        app.show_page("PreFlightPage")
    elif text.startswith("LoginAuth:"):
        app.pages["LoginPage"].error_var.set(text)


    global download_status
    ## Check for NACK responses
    if text.startswith("Request denied in state:"):
        _, error_msg = text.split(":", 1)
        download_status["ready"] = False
        download_status["filename"] = None
        download_status["error"] = error_msg.strip()

    elif text.startswith("Flight Manual:") and "downloaded" not in text:
        _, error_msg = text.split(":", 1)
        download_status["ready"] = False
        download_status["filename"] = None
        download_status["error"] = error_msg.strip()

    ## Check for ACK responses
    elif text.startswith("Flight Manual downloaded"):
        filename = text.split("to", 1)[1].strip()
        download_status["ready"] = True
        download_status["filename"] = filename
        download_status["error"] = None

        # Refresh PDF viewer if user is on that page
        try:
            app.pages["PDFViewerPage"].refresh()
        except:
            pass

    # Send logs to whichever page is active
    try:
        app.pages["PreFlightPage"].log_panel.add(text)
        app.pages["ActiveAirspacePage"].log_panel.add(text)
    except:
        pass

def send_cmd(cmd):
    api.send(cmd)

# ├── class Page
class Page(tk.Frame):
    """Base class for all pages."""
    def __init__(self, parent, controller):
        super().__init__(parent)
        self.controller = controller

# ├── class App
class App(tk.Tk):
    def __init__(self, api):
        super().__init__()
        self.api = api
        self.title("ClientApp UI")
        self.geometry("900x600")

        container = tk.Frame(self)
        container.pack(fill="both", expand=True)

        self.pages = {}

        for P in (LoginPage, PreFlightPage, ActiveAirspacePage, PDFViewerPage):
            page = P(container, self)
            self.pages[P.__name__] = page
            page.grid(row=0, column=0, sticky="nsew")

        self.show_page("LoginPage")

    def show_page(self, name):
        page = self.pages[name]
        page.tkraise()

# ├── class TopBar
class TopBar(ttk.Frame):
    def __init__(self, parent, controller):
        super().__init__(parent)
        self.controller = controller

        ttk.Button(self, text="Logout", command=lambda: controller.show_page("LoginPage")).pack(side="left", padx=5)
        ttk.Button(self, text="EMERGENCY", style="Danger.TButton",
                   command=lambda: controller.api.send("EMERGENCY")).pack(side="right", padx=5)
        
        style = ttk.Style()
        style.configure("Danger.TButton", foreground="white", background="red")

# ├── class LogPanel
class LogPanel(ttk.Frame):
    def __init__(self, parent):
        super().__init__(parent)

        self.text = tk.Text(self, height=12, state="disabled")
        scroll = ttk.Scrollbar(self, command=self.text.yview)
        self.text.configure(yscrollcommand=scroll.set)

        self.text.pack(side="left", fill="both", expand=True)
        scroll.pack(side="right", fill="y")

    def add(self, msg):
        self.text.config(state="normal")
        self.text.insert("end", msg + "\n")
        self.text.see("end")
        self.text.config(state="disabled")

# ├── class LoginPage
class LoginPage(Page):
    def __init__(self, parent, controller):
        super().__init__(parent, controller)

        ttk.Label(self, text="Login", font=("Arial", 20)).pack(pady=20)

        self.user_var = tk.StringVar()
        self.pass_var = tk.StringVar()
        self.error_var = tk.StringVar()

        ttk.Entry(self, textvariable=self.user_var).pack(pady=5)
        ttk.Entry(self, textvariable=self.pass_var, show="*").pack(pady=5)

        ttk.Button(self, text="Login", command=self.try_login).pack(pady=10)
        ttk.Label(self, textvariable=self.error_var, foreground="red").pack()

    def try_login(self):
        user = self.user_var.get()
        pw = self.pass_var.get()
        self.controller.api.send(f"LOGIN {user} {pw}")

# ├── class PreFlightPage
class PreFlightPage(Page):
    def __init__(self, parent, controller):
        super().__init__(parent, controller)

        # Top bar
        TopBar(self, controller).pack(fill="x")

        # Log panel
        self.log_panel = LogPanel(self)
        self.log_panel.pack(fill="both", expand=True, pady=10)

        # Button row
        btns = ttk.Frame(self)
        btns.pack()

        ttk.Button(btns, text="Weather", command=self.ask_weather).grid(row=0, column=0, padx=5)
        ttk.Button(btns, text="Flight Plan", command=self.ask_flight).grid(row=0, column=1, padx=5)
        ttk.Button(btns, text="Taxi Request", command=lambda: controller.api.send("TAXI")).grid(row=0, column=2, padx=5)
        ttk.Button(btns, text="Aircraft Manual", command=lambda: controller.show_page("PDFViewerPage")).grid(row=0, column=3, padx=5)

    def ask_weather(self):
        # TODO: popup for airport code
        self.controller.api.send("WEATHER YYZ")

    def ask_flight(self):
        # TODO: popup for flight ID
        self.controller.api.send("FLIGHT AC123")

# ├── class ActiveAirspacePage
class ActiveAirspacePage(Page):
    def __init__(self, parent, controller):
        super().__init__(parent, controller)

        TopBar(self, controller).pack(fill="x")

        self.log_panel = LogPanel(self)
        self.log_panel.pack(fill="both", expand=True, pady=10)

        btns = ttk.Frame(self)
        btns.pack()

        ttk.Button(btns, text="Telemetry Update", command=lambda: controller.api.send("TELEMETRY")).grid(row=0, column=0, padx=5)
        ttk.Button(btns, text="Airtraffic Request", command=lambda: controller.api.send("AIRTRAFFIC")).grid(row=0, column=1, padx=5)
        ttk.Button(btns, text="Clear Runway", command=lambda: controller.api.send("CLEAR")).grid(row=0, column=2, padx=5)
        ttk.Button(btns, text="Aircraft Manual", command=lambda: controller.show_page("PDFViewerPage")).grid(row=0, column=3, padx=5)

# ├── class PDFViewerPage
class PDFViewerPage(Page):
    def __init__(self, parent, controller):
        super().__init__(parent, controller)

        TopBar(self, controller).pack(fill="x")

        self.display = ttk.Frame(self)
        self.display.pack(fill="both", expand=True)

        self.refresh()

    def refresh(self):
        for w in self.display.winfo_children():
            w.destroy()

        # If no file downloaded yet
        if not download_status["filename"]:
            ttk.Button(self.display, text="Download Manual",
                    command=lambda: self.controller.api.send("MANUAL")).pack(expand=True)
            return

        pdf_path = os.path.join(BASE_DIR, download_status["filename"])

        if os.path.exists(pdf_path):
            viewer = PDFViewer(self.display, pdf_path)
            viewer.pack(fill="both", expand=True)
        else:
            ttk.Button(self.display, text="Download Manual",
                    command=lambda: self.controller.api.send("MANUAL")).pack(expand=True)

app = App(api)
threading.Thread(target=api.read_loop, args=(on_response,), daemon=True).start()
app.mainloop()

