from email.mime import text
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


    output_box.config(state="normal")
    output_box.insert("end", text + "\n")
    output_box.config(state="disabled")

def send_cmd(cmd):
    api.send(cmd)

def pdf_display_page():
    """Opens a subpage for viewing or downloading a PDF."""
    sub = tk.Toplevel(root)
    sub.title("PDF Viewer")
    sub.geometry("800x600")

    # --- Top bar with Back button ---
    top_bar = ttk.Frame(sub)
    top_bar.pack(fill="x")

    ttk.Button(top_bar, text="Back", command=sub.destroy).pack(side="left")

    # --- Display Area ---
    display = ttk.Frame(sub)
    display.pack(fill="both", expand=True)

    def refresh_display():
        """Refresh the display area depending on download status."""
        for w in display.winfo_children():
            w.destroy()

        # PDF Exists locally but not identified as ready (previously downloaded but not updated in status) → show viewer
        # - check if a .pdf file exists in the directory that matches the expected filename pattern (e.g., "flightmanual.pdf")
        # - if found, set download_status to ready with that filename and show viewer
        if not download_status["ready"]:
            for file in os.listdir(BASE_DIR):
                if file.lower().endswith(".pdf") and "flightmanual" in file.lower():
                    download_status["ready"] = True
                    download_status["filename"] = file
                    download_status["error"] = None
                    break

        # Case 1: PDF is ready → show viewer
        if download_status["ready"] and download_status["filename"]:
            pdf_path = os.path.join(BASE_DIR, download_status["filename"])
            if os.path.exists(pdf_path):
                viewer = PDFViewer(display, pdf_path)
                viewer.pack(fill="both", expand=True)
                return

        # Case 2: Error occurred → show error message + retry button
        if download_status["error"]:
            ttk.Label(display, text=f"Error: {download_status['error']}", foreground="red").pack(pady=20)
            ttk.Button(display, text="Retry Download", command=download_pdf).pack()
            return

        # Case 3: No PDF downloaded yet → show download button
        ttk.Button(display, text="Download PDF", command=download_pdf).pack(expand=True)

    def download_pdf():
        """Send MANUAL request and wait for ACK/NACK."""
        download_status["ready"] = False
        download_status["filename"] = None
        download_status["error"] = None

        send_cmd("MANUAL")

        # Poll for ACK/NACK
        def poll():
            if download_status["ready"] or download_status["error"]:
                refresh_display()
            else:
                sub.after(300, poll)

        poll()

    refresh_display()


root = tk.Tk()
root.title("ClientApp UI")

ttk.Button(root, text="Emergency", command=lambda: send_cmd("EMERGENCY")).pack(fill="x")    ## for now these are all hardcoded, but eventually we can add input fields for the user to specify args
ttk.Button(root, text="Weather Toronto", command=lambda: send_cmd("WEATHER YYZ")).pack(fill="x")
ttk.Button(root, text="Flight AC123", command=lambda: send_cmd("FLIGHT AC123")).pack(fill="x")
ttk.Button(root, text="Login Ryan", command=lambda: send_cmd("LOGIN RHackbart91 Gamma789")).pack(fill="x")
ttk.Button(root, text="Open Flight Manual", command=lambda: pdf_display_page()).pack(fill="x")

output_box = tk.Text(root, height=15, width=60, state="disabled")
output_box.pack(pady=10)



threading.Thread(target=api.read_loop, args=(on_response,), daemon=True).start()

root.mainloop()
