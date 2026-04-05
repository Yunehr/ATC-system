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
    elif text.startswith("LoginAuth:") or text.startswith("Authentication Failed"):
        app.pages["LoginPage"].error_var.set(text)

    # Taxi ACK/NACK
    if text.startswith("Taxi Clearance: APPROVED"):
        app.show_page("ActiveAirspacePage")
    elif text.startswith("Landing Clearance: APPROVED"):
        app.show_page("PreFlightPage")

     # Download ACK/NACK for Flight Manual
    global download_status
    ## Check for NACK responses
    if text.startswith("Request denied in state:"):
        _, error_msg = text.split(":", 1)
        download_status["ready"] = False
        download_status["filename"] = None
        download_status["error"] = error_msg.strip()

        # stop spinner if error occurs during download
        try:
            app.pages["PDFViewerPage"].stop_loading()
        except:
            pass

    elif text.startswith("Flight Manual:") and "downloaded" not in text:
        _, error_msg = text.split(":", 1)
        download_status["ready"] = False
        download_status["filename"] = None
        download_status["error"] = error_msg.strip()

        # stop spinner if error occurs during download
        try:
            app.pages["PDFViewerPage"].stop_loading()
        except:
            pass

    ## Check for ACK responses
    elif text.startswith("Flight Manual downloaded"):
        filename = text.split("to", 1)[1].strip()
        download_status["ready"] = True
        download_status["filename"] = filename
        download_status["error"] = None

        try:
            app.pages["PDFViewerPage"].stop_loading()
        except:
            pass

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

# ├── class TopBar
class TopBar(ttk.Frame):
    def __init__(self, parent, controller, page_name):
        super().__init__(parent)
        self.controller = controller

        self.configure(padding=(3, 3))

        # Left: Logout /Back button
        if page_name in ("PreFlightPage"):
            left_btn = ttk.Button(self, text="Logout",
                                  command=lambda: controller.show_page("LoginPage"))
        elif page_name == "PDFViewerPage":
            left_btn = ttk.Button(self, text="Back", # returns to precious page (either PreFlight or ActiveAirspace)
                                  command=lambda: controller.show_page(controller.previous_page))
        else:
            left_btn = None

        if left_btn:
            left_btn.pack(side="left", padx=5)
        
        # Right: Emergency
        emergency_btn = ttk.Button(
            self,
            text="EMERGENCY",
            command=lambda: controller.api.send("EMERGENCY")
        )
        emergency_btn.pack(side="right", padx=5)


# ├── class LogPanel
class LogPanel(ttk.Frame):
    def __init__(self, parent):
        super().__init__(parent)

        self.text = tk.Text(self, height=15, state="disabled")
        scroll = ttk.Scrollbar(self, command=self.text.yview)
        self.text.configure(yscrollcommand=scroll.set)

        self.text.pack(side="left", fill="both", expand=True, padx=10, pady=10)
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

        outer = ttk.Frame(self)
        outer.pack(expand=True, padx=40)

        ttk.Label(outer, text="Login", font=("Arial", 20)).pack(pady=20)

        self.user_var = tk.StringVar()
        self.pass_var = tk.StringVar()
        self.error_var = tk.StringVar()

        ttk.Entry(outer, textvariable=self.user_var, width=25).pack(pady=8)
        ttk.Entry(outer, textvariable=self.pass_var, show="*", width=25).pack(pady=8)

        ttk.Button(outer, text="Login", command=self.try_login).pack(pady=15)
        ttk.Label(outer, textvariable=self.error_var, foreground="red").pack()

    def try_login(self):
        user = self.user_var.get()
        pw = self.pass_var.get()
        self.controller.api.send(f"LOGIN {user} {pw}")

# ├── class PreFlightPage
class PreFlightPage(Page):
    def __init__(self, parent, controller):
        super().__init__(parent, controller)

        # Top bar
        TopBar(self, controller, page_name="PreFlightPage").pack(fill="x")

        # Log panel
        self.log_panel = LogPanel(self)
        self.log_panel.pack(fill="both", expand=True, pady=10)

        # Button row
        btns = ttk.Frame(self)
        btns.pack(pady=10)

        ttk.Button(btns, text="Flight Plan", width=20, command=self.ask_flight).pack(pady=5)
        ttk.Button(btns, text="Weather", width=20, command=self.ask_weather).pack(pady=5)
        ttk.Button(btns, text="Taxi Request", width=20,
                command=lambda: controller.api.send("TAXI")).pack(pady=5)
        ttk.Button(btns, text="Aircraft Manual", width=20,
                command=lambda: controller.show_page("PDFViewerPage")).pack(pady=5)
    
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

        TopBar(self, controller, page_name="ActiveAirspacePage").pack(fill="x")

        self.log_panel = LogPanel(self)
        self.log_panel.pack(fill="both", expand=True, pady=10)

        btns = ttk.Frame(self)
        btns.pack(pady=10)

        ttk.Button(btns, text="Telemetry Update", width=20,
                command=lambda: controller.api.send("TELEM")).pack(pady=5)
        ttk.Button(btns, text="Airtraffic Request", width=20,
                command=lambda: controller.api.send("TRAFFIC")).pack(pady=5)
        ttk.Button(btns, text="Clear Runway", width=20,
                command=lambda: controller.api.send("TAXI")).pack(pady=5)
        ttk.Button(btns, text="Aircraft Manual", width=20,
                command=lambda: controller.show_page("PDFViewerPage")).pack(pady=5)

# ├── class PDFViewerPage
class PDFViewerPage(Page):
    def __init__(self, parent, controller):
        super().__init__(parent, controller)

        TopBar(self, controller, page_name="PDFViewerPage").pack(fill="x")

        self.display = ttk.Frame(self)
        self.display.pack(fill="both", expand=True)
        self.display.pack(fill="both", expand=True, padx=10, pady=10)
        
        self.loading = False
        self.loading_label = None
        self.loading_step = 0

        self.refresh()

    def refresh(self):
        if self.loading:
            return # don't refresh if we're in the middle of downloading
        
        for w in self.display.winfo_children():
            w.destroy()

        # If no file downloaded yet
        if not download_status["filename"]:
            ttk.Button(self.display, text="Download Manual",
                command=self.download_manual).pack(expand=True)

            return

        pdf_path = os.path.join(BASE_DIR, download_status["filename"])

        if os.path.exists(pdf_path):
            viewer = PDFViewer(self.display, pdf_path)
            viewer.pack(fill="both", expand=True)
        else:
            ttk.Button(self.display, text="Download Manual",
                command=self.download_manual).pack(expand=True)
  
    def start_loading(self):
        self.loading = True

        # Clear display
        for w in self.display.winfo_children():
            w.destroy()

        # Create animated label
        self.loading_label = ttk.Label(self.display, text="Downloading.")
        self.loading_label.pack(expand=True, pady=20)

        # Start animation loop
        self.animate_loading()

    def animate_loading(self):
        if not self.loading:
            return

        dots = ["Downloading.", "Downloading..", "Downloading..."]
        self.loading_label.config(text=dots[self.loading_step])

        self.loading_step = (self.loading_step + 1) % 3

        # Schedule next frame
        self.after(500, self.animate_loading)


    def stop_loading(self):
        self.loading = False

        if self.loading_label:
            self.loading_label.destroy()
            self.loading_label = None

        self.refresh()


    def download_manual(self):
        # Start spinner immediately
        self.start_loading()

        self.display.update_idletasks()  # ensure spinner shows before sending command

        # Send command to backend
        self.controller.api.send("MANUAL")



# ├── class App
class App(tk.Tk):
    def __init__(self, api):
        super().__init__()
        self.api = api
        self.title("ClientApp UI")
        self.geometry("500x650")
        self.minsize(400, 600)

        self.eval('tk::PlaceWindow . center')  # Center the window on the screen

        container = tk.Frame(self)
        container.pack(fill="both", expand=True)
        container.grid_rowconfigure(0, weight=1)
        container.grid_columnconfigure(0, weight=1)

        self.pages = {}
        self.previous_page = None

        for P in (LoginPage, PreFlightPage, ActiveAirspacePage, PDFViewerPage):
            page = P(container, self)
            self.pages[P.__name__] = page
            page.grid(row=0, column=0, sticky="nsew")

        self.show_page("LoginPage")

    def show_page(self, name):
        if hasattr(self, "current_page"):
            self.previous_page = self.current_page

        self.current_page = name
        page = self.pages[name]
        page.tkraise()


app = App(api)
threading.Thread(target=api.read_loop, args=(on_response,), daemon=True).start()
app.mainloop()

