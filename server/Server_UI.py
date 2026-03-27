import tkinter as tk
from tkinter import ttk

class ATCScreen:
    def __init__(self, root):
        self.root = root
        self.root.title("ATC Tower - Server Dashboard")
        self.root.geometry("800x600")
        self.root.configure(bg="#1e1e1e") # High contrast dark mode (REQ-USE-010)

        # 1. Header / State Display (REQ-SVR-020)
        self.state_label = tk.Label(root, text="SYSTEM STATE: STARTUP", 
                                    fg="#00ff00", bg="#1e1e1e", font=("Courier", 18, "bold"))
        self.state_label.pack(pady=10)

        # 2. Main Content Area (Columns)
        self.main_frame = tk.Frame(root, bg="#1e1e1e")
        self.main_frame.pack(expand=True, fill="both", padx=10)

        # Left Column: Connection Monitor (REQ-SVR-010)
        self.conn_frame = tk.LabelFrame(self.main_frame, text="Active Aircraft", 
                                        fg="white", bg="#1e1e1e")
        self.conn_frame.pack(side="left", fill="y", padx=5)
        
        self.aircraft_list = tk.Listbox(self.conn_frame, width=20, bg="#2d2d2d", fg="white")
        self.aircraft_list.pack(expand=True, fill="both", padx=5, pady=5)
        self.aircraft_list.insert(tk.END, "No connections...")

        # Right Column: Audit Log View (REQ-LOG-010)
        self.log_frame = tk.LabelFrame(self.main_frame, text="Safety Audit Log (TX/RX)", 
                                       fg="white", bg="#1e1e1e")
        self.log_frame.pack(side="right", expand=True, fill="both", padx=5)

        self.log_text = tk.Text(self.log_frame, state="disabled", bg="black", fg="#00ff00", font=("Consolas", 10))
        self.log_text.pack(expand=True, fill="both", padx=5, pady=5)

        self.progress_frame = tk.Frame(root, bg="#1e1e1e")
        self.progress_frame.pack(fill="x", padx=10, pady=5)

        self.progress_label = tk.Label(self.progress_frame, text="File Transfer: Idle", 
                                       fg="white", bg="#1e1e1e", font=("Consolas", 9))
        self.progress_label.pack(side="top", anchor="w")

        self.progress = ttk.Progressbar(self.progress_frame, orient="horizontal", 
                                        length=100, mode="determinate")
        self.progress.pack(fill="x", expand=True)

        # 3. Manual Controls (REQ-SYS-060)
        self.ctrl_frame = tk.Frame(root, bg="#1e1e1e")
        self.ctrl_frame.pack(fill="x", side="bottom", pady=10)

        self.emergency_btn = tk.Button(self.ctrl_frame, text="DECLARE EMERGENCY", 
                                       bg="red", fg="white", command=self.trigger_emergency)
        self.emergency_btn.pack(side="right", padx=20)

    def add_log(self, message):
        """Adds a line to the Audit Log (REQ-LOG-010)"""
        self.log_text.config(state="normal")
        self.log_text.insert(tk.END, f"> {message}\n")
        self.log_text.see(tk.END)
        self.log_text.config(state="disabled")
    
    def update_progress(self, current, total):
        """Updates the progress bar for REQ-SYS-070 transfers"""
        percent = (current / total) * 100
        self.progress['value'] = percent
        self.progress_label.config(text=f"File Transfer: {int(percent)}%")
        self.root.update_idletasks() # Forces the UI to refresh immediately

    def trigger_emergency(self):
        """Example action for REQ-USE-020"""
        self.state_label.config(text="SYSTEM STATE: EMERGENCY", fg="red")
        self.add_log("MANUAL EMERGENCY DECLARED BY TOWER")

if __name__ == "__main__":
    root = tk.Tk()
    app = ATCScreen(root)
    # Manual update for testing
    app.add_log("ATC Server Engine Initialized...")
    root.mainloop()