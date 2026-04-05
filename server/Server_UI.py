## @file Server_UI.py
#  @brief Tkinter-based ATC Tower Control Center dashboard.
#
#  This module provides the graphical monitoring interface for the aviation
#  backend server.  It spawns the C++ server executable as a child process,
#  reads its stdout in a daemon thread, and presents live data across four
#  areas:
#  - A metrics row (connections, uptime, data transferred, last activity).
#  - An active aircraft panel populated from real-time @c netstat output.
#  - A system log panel (REQ-LOG-010).
#  - A file transfer progress bar (REQ-SYS-070).
#
#  Server log lines are parsed for structured events (@c STATE:,
#  @c TRANSFER START:, @c TRANSFER PROGRESS:, @c TRANSFER COMPLETE:) and
#  dispatched to the appropriate UI handlers via @c root.after() so all
#  widget updates remain on the Tk main thread.
import tkinter as tk
from tkinter import ttk
import tkinter.font as tkFont
import subprocess
import threading
import os
from datetime import datetime
import queue
import time
## @class ATCScreen
#  @brief Main application class for the ATC Tower Control Center dashboard.
#
#  Responsibilities:
#  - Launching and monitoring the C++ server subprocess.
#  - Reading server stdout on a daemon thread and routing messages to the UI
#    via thread-safe queues.
#  - Polling @c netstat every second to display live TCP connection endpoints.
#  - Updating metric cards, the system log, and the file transfer progress bar.
#  - Cleanly terminating the server process on window close.
class ATCScreen:
# -------------------------------------------------------------------------
    # Class-level constants
    # -------------------------------------------------------------------------
 
    ## @brief Deep navy background colour used for the root window and main frames.
    PRIMARY_BG = "#0f1419"      # Deep navy background
    ## @brief Slightly lighter background colour used for card/panel frames.
    SECONDARY_BG = "#1a1f2e"    # Slightly lighter for cards
    ## @brief Bright cyan accent colour used for titles, borders, and metric values.    
    ACCENT_BLUE = "#00d4ff"     # Bright cyan accent
    ## @brief Fresh green accent colour used for active status indicators and log text.
    ACCENT_GREEN = "#00ff85"    # Fresh green for active
    ## @brief Warning orange used for offline/error status indicators.    
    ACCENT_ORANGE = "#ff6b35"   # Warning orange
    ## @brief Primary white text colour.   
    TEXT_PRIMARY = "#ffffff"    # White text
    ## @brief Muted grey colour for secondary labels and captions.
    TEXT_SECONDARY = "#a0aec0"  # Muted gray text
    ## @brief Subtle border colour used for separator lines and card outlines.    
    BORDER_COLOR = "#2d3748"    # Subtle borders
    ## @brief TCP port the C++ server listens on; used by the @c netstat poll.    
    SERVER_PORT = 8080
    
    
    # -------------------------------------------------------------------------
    # Construction
    # -------------------------------------------------------------------------
 
    ## @brief Constructs the ATCScreen, builds the UI, and starts the server.
    #
    #  Initialises all instance state, configures ttk styles and fonts,
    #  creates all UI sections, launches the C++ server subprocess, and
    #  starts the background threads and Tk polling loops.
    #
    #  @param root The root @c tk.Tk window instance.
    def __init__(self, root):
        self.root = root
        self.root.title("ATC Tower Control Center")
        self.root.geometry("1400x850")
        self.root.configure(bg=self.PRIMARY_BG)
        
        ## @brief Thread-safe queue for server stdout lines to be added to the log panel.
        self.log_queue = queue.Queue()
        ## @brief Thread-safe queue for connection add/remove events.        
        self.connection_queue = queue.Queue()
        
        ## @brief The running C++ server subprocess, or None if not started.
        self.server_process = None
        ## @brief True while the server subprocess is running.        
        self.server_running = False
        ## @brief Dict mapping connection index to display label string.        
        self.active_connections = {}
        ## @brief Timestamp of when the server was started, used to compute uptime.
        self.server_start_time = None
        ## @brief Dict mapping metric card label strings to their tk.Label widgets.
        self.metric_values = {}
        ## @brief Running total of bytes transferred, updated by handle_transfer_event().
        self.data_transferred_bytes = 0
        ## @brief Total file size in bytes for the current transfer, or 0 if idle.        
        self.transfer_total_bytes = 0
        ## @brief Bytes transferred so far in the current file transfer.        
        self.transfer_current_bytes = 0
        
        # Configure styles
        self.setup_styles()
        
        # Header Section
        self.create_header()
        
        # Main Content Area
        self.create_main_content()
        
        # Footer Section
        self.create_footer()
        
        # Start the C++ server
        self.start_server()
        
        # Start UI update thread
        self.start_ui_update_thread()
        
        # Start polling for server updates
        self.poll_server_output()
        self.poll_connections()
        
        # Cleanup on exit
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)

    # -------------------------------------------------------------------------
    # Server lifecycle
    # -------------------------------------------------------------------------
 
    ## @brief Locates and launches the C++ server executable as a subprocess.
    #
    #  Resolves the path to @c server.exe relative to this script's directory,
    #  spawns it with stdout/stderr merged into a single pipe, and starts
    #  @ref read_server_output in a daemon thread.
    #
    #  Logs an error to the UI panel if the executable is not found or the
    #  process fails to start.    
    def start_server(self):
        """Launch the C++ server executable"""
        try:
            # Get the build directory
            current_dir = os.path.dirname(os.path.abspath(__file__))
            build_dir = os.path.join(os.path.dirname(current_dir), "build")
            server_exe = os.path.join(build_dir, "server.exe")
            
            if not os.path.exists(server_exe):
                self.add_log(f"❌ ERROR: Server executable not found at {server_exe}")
                return
            
            # Start the server process
            self.server_process = subprocess.Popen(
                [server_exe],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                universal_newlines=True
            )
            
            self.server_running = True
            self.server_start_time = datetime.now()
            self.add_log("✓ ATC Server Engine Started")
            
            # Start thread to read server output
            self.server_thread = threading.Thread(target=self.read_server_output, daemon=True)
            self.server_thread.start()
            
        except Exception as e:
            self.add_log(f"❌ Failed to start server: {str(e)}")
            self.server_running = False

    ## @brief Reads server stdout lines on a daemon thread and queues them for the UI.
    #
    #  For each line received:
    #  - Pushes the raw text to @ref log_queue for display in the log panel.
    #  - Calls @ref parse_transfer_event and, if a transfer event is found,
    #    schedules @ref handle_transfer_event on the Tk main thread.
    #  - Detects @c "STATE:" prefixed lines and schedules @ref set_system_state.
    #  - Detects connection/disconnection keywords and pushes events to
    #    @ref connection_queue.
    #
    #  Exits when @ref server_running is False or the stdout stream closes.    
    def read_server_output(self):
        """Read server output in a separate thread"""
        try:
            while self.server_running and self.server_process:
                line = self.server_process.stdout.readline()
                if line:
                    clean_line = line.strip()
                    self.log_queue.put(clean_line)

                    transfer_event = self.parse_transfer_event(clean_line)
                    if transfer_event:
                        event_type, current, total = transfer_event
                        self.root.after(0, lambda et=event_type, c=current, t=total: self.handle_transfer_event(et, c, t))
                    
                    # Parse state changes
                    if clean_line.startswith("STATE:"):
                        state = clean_line.split(":", 1)[1].strip()
                        self.root.after(0, lambda s=state: self.set_system_state(s))

                    # Parse connection info
                    lower_line = clean_line.lower()
                    if "client connected" in lower_line:
                        self.connection_queue.put(("connect", "New Aircraft"))
                    elif "client disconnected" in lower_line:
                        self.connection_queue.put(("disconnect", None))
        except Exception as e:
            self.log_queue.put(f"❌ Error reading server output: {str(e)}")

    # -------------------------------------------------------------------------
    # Background threads and polling
    # -------------------------------------------------------------------------
 
    ## @brief Starts the daemon thread that updates the SYSTEM UPTIME metric every second.    
    def start_ui_update_thread(self):
        """Start thread for periodic UI updates"""
        def update_uptime():
            while self.server_running:
                time.sleep(1)
                if self.server_start_time:
                    uptime = datetime.now() - self.server_start_time
                    hours = int(uptime.total_seconds() // 3600)
                    minutes = int((uptime.total_seconds() % 3600) // 60)
                    seconds = int(uptime.total_seconds() % 60)
                    self.root.after(0, lambda h=hours, m=minutes, s=seconds: self.set_metric("SYSTEM UPTIME", f"{h:02}:{m:02}:{s:02}"))
        
        uptime_thread = threading.Thread(target=update_uptime, daemon=True)
        uptime_thread.start()

    ## @brief Polls @ref log_queue and @ref connection_queue every 100 ms and updates the UI.
    #
    #  Also detects if the server process has exited unexpectedly and updates
    #  the footer status indicator accordingly.  Reschedules itself via
    #  @c root.after() while @ref server_running is True.    
    def poll_server_output(self):
        """Poll for server output and update UI"""
        if self.server_process and self.server_running and self.server_process.poll() is not None:
            self.server_running = False
            self.add_log("❌ Server process exited")
            self.status_text.config(text="● System: Offline", fg=self.ACCENT_ORANGE)

        try:
            # Process log queue
            while not self.log_queue.empty():
                log_msg = self.log_queue.get_nowait()
                self.add_log(log_msg)
                self.update_last_activity()
        except queue.Empty:
            pass
        
        try:
            # Process connection queue
            while not self.connection_queue.empty():
                action, data = self.connection_queue.get_nowait()
                if action == "connect":
                    self.add_connection(data)
                elif action == "disconnect":
                    self.remove_connection()
        except queue.Empty:
            pass
        
        # Schedule next poll (every 100ms)
        if self.server_running:
            self.root.after(100, self.poll_server_output)

    ## @brief Polls active TCP connections via @c netstat every second and refreshes the aircraft list.
    #
    #  Filters @c netstat output for @c ESTABLISHED connections on @ref SERVER_PORT
    #  whose PID matches the server process, then calls @ref refresh_connections
    #  with the sorted list of remote endpoints.
    #
    #  Reschedules itself via @c root.after() while @ref server_running is True.
    def poll_connections(self):
        """Poll active TCP connections for server port and refresh UI list."""
        if not self.server_running or not self.server_process:
            return

        try:
            result = subprocess.run(
                ["netstat", "-ano", "-p", "tcp"],
                capture_output=True,
                text=True,
                check=False
            )

            endpoints = []
            server_pid = str(self.server_process.pid)
            local_suffix = f":{self.SERVER_PORT}"

            for line in result.stdout.splitlines():
                parts = line.split()
                if len(parts) < 5 or parts[0] != "TCP":
                    continue

                local_addr, remote_addr, state, pid = parts[1], parts[2], parts[3], parts[4]
                if state == "ESTABLISHED" and local_addr.endswith(local_suffix) and pid == server_pid:
                    endpoints.append(remote_addr)

            self.refresh_connections(sorted(set(endpoints)))
        except Exception as e:
            self.add_log(f"⚠ Connection poll failed: {str(e)}")

        if self.server_running:
            self.root.after(1000, self.poll_connections)

    # -------------------------------------------------------------------------
    # Connection management
    # -------------------------------------------------------------------------
 
    ## @brief Rebuilds the aircraft list from a fresh set of remote endpoint strings.
    #
    #  Clears @ref active_connections and the Listbox widget, then inserts one
    #  entry per endpoint.  Updates the ACTIVE CONNECTIONS metric card.
    #
    #  @param endpoints Sorted list of @c "ip:port" strings for established connections.
    def refresh_connections(self, endpoints):
        """Render connection list from currently established remote endpoints."""
        self.aircraft_list.delete(0, tk.END)
        self.active_connections.clear()

        if not endpoints:
            self.aircraft_list.insert(tk.END, "No active connections")
            self.set_metric("ACTIVE CONNECTIONS", "0")
            return

        for idx, endpoint in enumerate(endpoints):
            label = f"Aircraft {idx + 1}  {endpoint}"
            self.aircraft_list.insert(tk.END, label)
            self.active_connections[idx] = label

        self.set_metric("ACTIVE CONNECTIONS", str(len(endpoints)))

    ## @brief Appends a new connection entry to the aircraft Listbox.
    #
    #  Removes the "No active connections" placeholder if present.
    #
    #  @param name Display name for the new connection (e.g. @c "New Aircraft").    
    def add_connection(self, name):
        """Add a connection to the aircraft list"""
        if "No active connections" in self.aircraft_list.get(0, tk.END):
            self.aircraft_list.delete(0, tk.END)
        
        timestamp = datetime.now().strftime("%H:%M:%S")
        connection_id = len(self.aircraft_list.get(0, tk.END))
        connection_label = f"[{timestamp}] {name}"
        self.aircraft_list.insert(tk.END, connection_label)
        self.active_connections[connection_id] = connection_label


    ## @brief Removes the most recent entry from the aircraft Listbox.
    #
    #  Re-inserts the "No active connections" placeholder if the list becomes empty.    
    def remove_connection(self):
        """Remove a connection from the aircraft list"""
        if self.aircraft_list.size() > 0:
            self.aircraft_list.delete(self.aircraft_list.size() - 1)
        
        if self.aircraft_list.size() == 0:
            self.aircraft_list.insert(tk.END, "No active connections")

    # -------------------------------------------------------------------------
    # Window close
    # -------------------------------------------------------------------------
 
    ## @brief Terminates the server process and destroys the Tk window.
    #
    #  Attempts a graceful @c terminate() with a 2-second timeout before
    #  falling back to @c kill().    
    def on_closing(self):
        """Cleanup and exit"""
        self.server_running = False
        if self.server_process:
            try:
                self.server_process.terminate()
                self.server_process.wait(timeout=2)
            except:
                self.server_process.kill()
        self.root.destroy()

    # -------------------------------------------------------------------------
    # UI construction
    # -------------------------------------------------------------------------
 
    ## @brief Configures ttk styles and defines the application font objects.    
    def setup_styles(self):
        """Configure modern color scheme and fonts"""
        style = ttk.Style()
        style.theme_use('clam')
        
        # Configure colors
        style.configure('TFrame', background=self.PRIMARY_BG)
        style.configure('TLabel', background=self.PRIMARY_BG, foreground=self.TEXT_PRIMARY)
        style.configure('Accent.TLabel', background=self.PRIMARY_BG, foreground=self.ACCENT_BLUE)
        style.configure('Horizontal.TProgressbar', background=self.ACCENT_GREEN, troughcolor=self.SECONDARY_BG)
        
  
        ## @brief Large bold font used for the "ATC TOWER" title.
        self.font_title = tkFont.Font(family="Segoe UI", size=24, weight="bold")
        ## @brief Medium bold font used for section headings and metric values.        
        self.font_subtitle = tkFont.Font(family="Segoe UI", size=11, weight="bold")
        ## @brief Standard body font.
        self.font_normal = tkFont.Font(family="Segoe UI", size=10)
        ## @brief Small font used for secondary labels, captions, and the footer.        
        self.font_small = tkFont.Font(family="Segoe UI", size=9)
        ## @brief Monospace font used for the log panel and aircraft list.        
        self.font_mono = tkFont.Font(family="Consolas", size=9)
    
    ## @brief Creates the header bar containing the title and system state indicator.    
    def create_header(self):
        """Create professional header with system state"""
        header_frame = tk.Frame(self.root, bg=self.SECONDARY_BG, height=80)
        header_frame.pack(fill="x", padx=0, pady=0)
        header_frame.pack_propagate(False)
        
        # Add subtle border
        border = tk.Frame(header_frame, bg=self.BORDER_COLOR, height=1)
        border.pack(fill="x", side="bottom")
        
        content_frame = tk.Frame(header_frame, bg=self.SECONDARY_BG)
        content_frame.pack(expand=True, fill="both", padx=25, pady=15)
        
        # Left side: Title
        left_frame = tk.Frame(content_frame, bg=self.SECONDARY_BG)
        left_frame.pack(side="left", fill="both", expand=True)
        
        title_label = tk.Label(left_frame, text="ATC TOWER", fg=self.ACCENT_BLUE, 
                              bg=self.SECONDARY_BG, font=self.font_title)
        title_label.pack(anchor="w")
        
        subtitle_label = tk.Label(left_frame, text="Control Center Dashboard", 
                                 fg=self.TEXT_SECONDARY, bg=self.SECONDARY_BG, 
                                 font=tkFont.Font(family="Segoe UI", size=10))
        subtitle_label.pack(anchor="w")
        
        # Right side: System State Indicator
        right_frame = tk.Frame(content_frame, bg=self.SECONDARY_BG)
        right_frame.pack(side="right", fill="y", padx=20)
        
        state_indicator_frame = tk.Frame(right_frame, bg=self.SECONDARY_BG)
        state_indicator_frame.pack(anchor="e")
        
        state_label_text = tk.Label(state_indicator_frame, text="SYSTEM STATE", 
                                   fg=self.TEXT_SECONDARY, bg=self.SECONDARY_BG, 
                                   font=self.font_small)
        state_label_text.pack(anchor="e")
        ## @brief Label widget displaying the current server state string.        
        self.state_label = tk.Label(state_indicator_frame, text="● STARTUP", 
                                   fg=self.ACCENT_GREEN, bg=self.SECONDARY_BG, 
                                   font=self.font_subtitle)
        self.state_label.pack(anchor="e", pady=(5, 0))
    
     ## @brief Creates the main content area: metrics row, aircraft/log panels, and progress bar.   
    def create_main_content(self):
        """Create the main content area with three sections"""
        main_frame = tk.Frame(self.root, bg=self.PRIMARY_BG)
        main_frame.pack(expand=True, fill="both", padx=20, pady=20)
        
        # Top row: Stats/Metrics
        self.create_metrics_row(main_frame)
        
        # Middle row: Aircraft List and Log (side by side)
        content_row = tk.Frame(main_frame, bg=self.PRIMARY_BG)
        content_row.pack(expand=True, fill="both", pady=(20, 0))
        
        self.create_aircraft_section(content_row)
        self.create_log_section(content_row)
        
        # Bottom: Progress Bar
        self.create_progress_section(main_frame)

    ## @brief Creates the four metric cards at the top of the main content area.
    #  @param parent The parent frame to attach the metrics row to.    
    def create_metrics_row(self, parent):
        """Create metric cards at the top"""
        metrics_frame = tk.Frame(parent, bg=self.PRIMARY_BG)
        metrics_frame.pack(fill="x", pady=(0, 20))
        
        # Metric card 1
        self.create_metric_card(metrics_frame, "ACTIVE CONNECTIONS", "0", 0)
        
        # Metric card 2
        self.create_metric_card(metrics_frame, "SYSTEM UPTIME", "00:00:00", 1)
        
        # Metric card 3
        self.create_metric_card(metrics_frame, "DATA TRANSFERRED", "0 MB", 2)
        
        # Metric card 4
        self.create_metric_card(metrics_frame, "LAST ACTIVITY", "--:--:--", 3)

    ## @brief Creates a single metric card widget and registers it in @ref metric_values.
    #
    #  @param parent   The parent frame (the metrics row).
    #  @param label    The metric name string (used as the key in @ref metric_values).
    #  @param value    The initial display value string.
    #  @param position Column position index (currently unused; cards are packed left-to-right).    
    def create_metric_card(self, parent, label, value, position):
        """Create a single metric card"""
        card = tk.Frame(parent, bg=self.SECONDARY_BG)
        card.pack(side="left", fill="both", expand=True, padx=7.5)
        
        # Top border accent
        border_top = tk.Frame(card, bg=self.ACCENT_BLUE, height=3)
        border_top.pack(fill="x")
        
        content = tk.Frame(card, bg=self.SECONDARY_BG)
        content.pack(fill="both", expand=True, padx=15, pady=15)
        
        label_widget = tk.Label(content, text=label, fg=self.TEXT_SECONDARY, 
                               bg=self.SECONDARY_BG, font=self.font_small)
        label_widget.pack(anchor="w")
        
        value_widget = tk.Label(content, text=value, fg=self.ACCENT_BLUE, 
                               bg=self.SECONDARY_BG, font=self.font_subtitle)
        value_widget.pack(anchor="w", pady=(5, 0))
        self.metric_values[label] = value_widget

    ## @brief Creates the "ACTIVE AIRCRAFT" panel with a scrollable Listbox.
    #  @param parent The parent content-row frame.    
    def create_aircraft_section(self, parent):
        """Create active aircraft panel"""
        aircraft_frame = tk.Frame(parent, bg=self.SECONDARY_BG, width=300)
        aircraft_frame.pack_propagate(False)
        aircraft_frame.pack(side="left", fill="both", padx=(0, 15), expand=False)
        
        # Header with icon
        header = tk.Frame(aircraft_frame, bg=self.SECONDARY_BG)
        header.pack(fill="x", padx=20, pady=20)
        
        title = tk.Label(header, text="✈ ACTIVE AIRCRAFT", fg=self.ACCENT_BLUE, 
                        bg=self.SECONDARY_BG, font=self.font_subtitle)
        title.pack(anchor="w")
        
       ## @brief Listbox displaying current aircraft connections.
        self.aircraft_list = tk.Listbox(aircraft_frame, bg=self.PRIMARY_BG, 
                                        fg=self.TEXT_PRIMARY, font=self.font_mono,
                                        bd=0, highlightthickness=0, 
                                        selectmode="extended", height=25)
        self.aircraft_list.pack(fill="both", expand=True, padx=20, pady=(0, 20))
        self.aircraft_list.insert(tk.END, "No active connections")
        
        # Scrollbar
        scrollbar = ttk.Scrollbar(aircraft_frame, orient="vertical", command=self.aircraft_list.yview)
        scrollbar.pack(side="right", fill="y", padx=(0, 20), pady=(0, 20))
        self.aircraft_list.config(yscrollcommand=scrollbar.set)

 
    ## @brief Creates the "SYSTEM LOG" panel with a scrollable read-only Text widget.
    #  @param parent The parent content-row frame.    
    def create_log_section(self, parent):
        """Create audit log panel"""
        log_frame = tk.Frame(parent, bg=self.SECONDARY_BG)
        log_frame.pack(side="right", fill="both", expand=True)
        
        # Header
        header = tk.Frame(log_frame, bg=self.SECONDARY_BG)
        header.pack(fill="x", padx=20, pady=20)
        
        title = tk.Label(header, text="📋 SYSTEM LOG", fg=self.ACCENT_BLUE, 
                        bg=self.SECONDARY_BG, font=self.font_subtitle)
        title.pack(anchor="w")
        
        ## @brief Read-only Text widget used as the system log display (REQ-LOG-010).
        self.log_text = tk.Text(log_frame, state="disabled", bg=self.PRIMARY_BG, 
                               fg=self.ACCENT_GREEN, font=self.font_mono,
                               bd=0, highlightthickness=0, wrap="word")
        self.log_text.pack(fill="both", expand=True, padx=20, pady=(0, 20))
        
        # Scrollbar for log
        scrollbar = ttk.Scrollbar(log_frame, orient="vertical", command=self.log_text.yview)
        scrollbar.pack(side="right", fill="y", padx=(0, 20), pady=(0, 20))
        self.log_text.config(yscrollcommand=scrollbar.set)

    ## @brief Creates the file transfer progress bar section at the bottom of the main area.
    #  @param parent The main content frame.    
    def create_progress_section(self, parent):
        """Create file transfer progress section"""
        progress_frame = tk.Frame(parent, bg=self.SECONDARY_BG)
        progress_frame.pack(fill="x", pady=(20, 0))
        
        # Border
        border_top = tk.Frame(progress_frame, bg=self.BORDER_COLOR, height=1)
        border_top.pack(fill="x")
        
        content = tk.Frame(progress_frame, bg=self.SECONDARY_BG)
        content.pack(fill="x", padx=20, pady=15)
        
        label_frame = tk.Frame(content, bg=self.SECONDARY_BG)
        label_frame.pack(fill="x", pady=(0, 10))
        
        header_row = tk.Frame(label_frame, bg=self.SECONDARY_BG)
        header_row.pack(fill="x")
        
        ## @brief Label showing the current file transfer status string.        
        self.progress_label = tk.Label(header_row, text="📁 File Transfer: Idle", 
                                      fg=self.ACCENT_BLUE, bg=self.SECONDARY_BG, 
                                      font=self.font_subtitle)
        self.progress_label.pack(anchor="w", side="left")

        ## @brief Label showing the current transfer percentage as text.        
        self.progress_percent = tk.Label(header_row, text="0%", 
                                        fg=self.TEXT_SECONDARY, bg=self.SECONDARY_BG, 
                                        font=self.font_small)
        self.progress_percent.pack(anchor="e", side="right")
        
        bar_frame = tk.Frame(content, bg=self.PRIMARY_BG, height=8)
        bar_frame.pack(fill="x", pady=(10, 0))

        ## @brief ttk Progressbar widget driven by @ref update_progress (REQ-SYS-070).        
        self.progress = ttk.Progressbar(bar_frame, orient="horizontal", 
                                        mode="determinate", length=500)
        self.progress.pack(fill="x", expand=True)
   ## @brief Creates the footer bar with the system online/offline indicator and version string.    
    def create_footer(self):
        """Create professional footer"""
        footer_frame = tk.Frame(self.root, bg=self.SECONDARY_BG, height=60)
        footer_frame.pack(fill="x", side="bottom")
        footer_frame.pack_propagate(False)
        
        # Top border
        border = tk.Frame(footer_frame, bg=self.BORDER_COLOR, height=1)
        border.pack(fill="x", side="top")
        
        content = tk.Frame(footer_frame, bg=self.SECONDARY_BG)
        content.pack(expand=True, fill="both", padx=25, pady=12)
        
        # Status indicators
        status_frame = tk.Frame(content, bg=self.SECONDARY_BG)
        status_frame.pack(side="left")

        ## @brief Label showing "● System: Online/Offline" in the footer.        
        self.status_text = tk.Label(status_frame, text="● System: Online", 
                                   fg=self.ACCENT_GREEN, bg=self.SECONDARY_BG, 
                                   font=self.font_small)
        self.status_text.pack(anchor="w")
        
        # Copyright
        copyright_label = tk.Label(content, text="ATC Control System v1.0 © 2026", 
                                  fg=self.TEXT_SECONDARY, bg=self.SECONDARY_BG, 
                                  font=self.font_small)
        copyright_label.pack(side="right", anchor="e")

    # -------------------------------------------------------------------------
    # UI update helpers
    # -------------------------------------------------------------------------
 
    ## @brief Appends a message to the system log panel (REQ-LOG-010).
    #
    #  Temporarily enables the read-only Text widget, inserts the message
    #  prefixed with @c "→ ", scrolls to the bottom, then disables it again.
    #
    #  @param message The log message string to append.    
    def add_log(self, message):
        """Adds a line to the Audit Log (REQ-LOG-010)"""
        self.log_text.config(state="normal")
        self.log_text.insert(tk.END, f"→ {message}\n")
        self.log_text.see(tk.END)
        self.log_text.config(state="disabled")
        self.root.update_idletasks()

    ## @brief Updates a metric card's value label by its header string.
    #
    #  @param metric_name The metric label string (must match a key in @ref metric_values).
    #  @param value       The new value string to display.
    def set_metric(self, metric_name, value):
        """Update a metric card value by label."""
        label = self.metric_values.get(metric_name)
        if label:
            label.config(text=value)

    ## @brief Parses a server log line for structured file-transfer progress events.
    #
    #  Recognises three prefixes emitted by the C++ server:
    #  - @c "TRANSFER START:total=<N>"      — a new transfer beginning with N total bytes.
    #  - @c "TRANSFER PROGRESS:current=<C> total=<T>" — an in-progress update.
    #  - @c "TRANSFER COMPLETE:current=<C> total=<T>" — the transfer has finished.
    #
    #  @param line A single stripped server output line.
    #  @return A @c (event_type, current, total) tuple on success, or @c None if
    #          the line does not match any transfer event pattern.
    def parse_transfer_event(self, line):
        """Extract file-transfer progress events from server log lines."""
        if line.startswith("TRANSFER START:"):
            try:
                total = int(line.split("total=", 1)[1].strip())
                return ("start", 0, total)
            except (IndexError, ValueError):
                return None

        if line.startswith("TRANSFER PROGRESS:"):
            try:
                rest = line.split(":", 1)[1].strip()
                parts = dict(part.split("=", 1) for part in rest.split() if "=" in part)
                current = int(parts.get("current", "0"))
                total = int(parts.get("total", "0"))
                return ("progress", current, total)
            except (ValueError, IndexError):
                return None

        if line.startswith("TRANSFER COMPLETE:"):
            try:
                rest = line.split(":", 1)[1].strip()
                parts = dict(part.split("=", 1) for part in rest.split() if "=" in part)
                current = int(parts.get("current", "0"))
                total = int(parts.get("total", "0"))
                return ("complete", current, total)
            except (ValueError, IndexError):
                return None

        return None

    ## @brief Updates the progress bar and DATA TRANSFERRED metric for a transfer event.
    #
    #  Dispatched from @ref read_server_output via @c root.after() so it always
    #  runs on the Tk main thread.
    #
    #  @param event_type One of @c "start", @c "progress", or @c "complete".
    #  @param current    Bytes transferred so far.
    #  @param total      Total file size in bytes.
    def handle_transfer_event(self, event_type, current, total):
        """Update the progress bar and transferred-byte metrics."""
        if event_type == "start":
            self.transfer_total_bytes = total
            self.transfer_current_bytes = 0
            self.progress['value'] = 0
            self.progress_label.config(text="📁 File Transfer: 0%")
            self.progress_percent.config(text="0%")
            return

        if total > 0:
            self.transfer_total_bytes = total
            self.transfer_current_bytes = current
            self.data_transferred_bytes = current
            self.update_progress(current, total)
            self.set_metric("DATA TRANSFERRED", self.format_bytes(self.data_transferred_bytes))

        if event_type == "complete":
            if total > 0:
                self.update_progress(total, total)
                self.data_transferred_bytes = total
                self.set_metric("DATA TRANSFERRED", self.format_bytes(self.data_transferred_bytes))
            self.progress_label.config(text="📁 File Transfer: Complete")

    ## @brief Formats a raw byte count into a human-readable string.
    #
    #  @param byte_count Number of bytes to format.
    #  @return A string in MB (≥ 1 MiB), KB (≥ 1 KiB), or B otherwise.
    def format_bytes(self, byte_count):
        """Format a byte count for the metric card."""
        if byte_count >= 1024 * 1024:
            return f"{byte_count / (1024 * 1024):.2f} MB"
        if byte_count >= 1024:
            return f"{byte_count / 1024:.1f} KB"
        return f"{byte_count} B"
    
    ## @brief Updates the LAST ACTIVITY metric card to the current wall-clock time.
    def update_last_activity(self):
        """Update the last activity metric to current time."""
        self.set_metric("LAST ACTIVITY", datetime.now().strftime("%H:%M:%S"))

    ## @brief Updates the progress bar and associated labels (REQ-SYS-070).
    #
    #  @param current Bytes transferred so far.
    #  @param total   Total file size in bytes.  If zero, percent is set to 0. 
    def update_progress(self, current, total):
        """Updates the progress bar for REQ-SYS-070 transfers"""
        if total == 0:
            percent = 0
        else:
            percent = (current / total) * 100
        
        self.progress['value'] = percent
        self.progress_label.config(text=f"📁 File Transfer: {int(percent)}%")
        self.progress_percent.config(text=f"{int(percent)}%")
        self.root.update_idletasks()
    ## @brief Updates the SYSTEM STATE indicator in the header.
    #
    #  @param state The new state string parsed from a @c "STATE:" server log line.    
    def set_system_state(self, state):
        """Update the system state display"""
        self.state_label.config(text=f"● {state}")

# -------------------------------------------------------------------------
# Entry point
# -------------------------------------------------------------------------
if __name__ == "__main__":
    root = tk.Tk()
    app = ATCScreen(root)
    # Manual update for testing
    app.add_log("Waiting for incoming connections...")
    root.mainloop()