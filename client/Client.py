## @file clientUI.py
#  @brief Tkinter-based client UI for the aviation client application.
#
#  This module provides the graphical front-end for a pilot client application.
#  It communicates with a backend executable (clientAPI.exe) via stdin/stdout,
#  and renders pages for login, pre-flight checks, active airspace operations,
#  and an in-app PDF manual viewer.

import tkinter as tk
from tkinter import ttk
import subprocess
import threading
import os
from pdfViewer import PDFViewer

## @brief Absolute path to the directory containing this script.
BASE_DIR = os.path.dirname(os.path.abspath(__file__))

## @brief Absolute path to the backend API executable.
API_EXE = os.path.join(BASE_DIR, "..", "build", "clientAPI.exe")
API_EXE = os.path.abspath(API_EXE)

## @brief Global dictionary tracking the state of a flight manual download.
#
#  Fields:
#  - @c ready    (bool)  : True if a file has been successfully downloaded.
#  - @c filename (str)   : Relative path to the downloaded file, or None.
#  - @c error    (str)   : Error message from the last failed download, or None.
download_status = {
    "ready": False,
    "filename": None,
    "error": None
}

## @class ClientAPI
#  @brief Manages the subprocess connection to the backend API executable.
#
#  Spawns clientAPI.exe as a child process and exposes methods to write
#  commands to its stdin and read lines from its stdout.
class ClientAPI:
    ## @brief Initialises the ClientAPI and launches the backend process.
    def __init__(self):
        ## @brief The backend subprocess handle.
        self.proc = subprocess.Popen(
            [API_EXE],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1, 
            cwd=os.path.dirname(API_EXE) # <-- set working directory to where the exe is
        )

    ## @brief Writes a newline-terminated command to the backend's stdin.
    #  @param msg The command string to send (without a trailing newline).
    def send(self, msg):
        self.proc.stdin.write(msg + "\n")
        self.proc.stdin.flush()

        ## @brief Continuously reads lines from the backend's stdout and dispatches them.
    #
    #  Intended to be run in a daemon thread.  Blocks until the process closes
    #  its stdout stream.
    #
    #  @param callback A callable that accepts a single string argument; invoked
    #  once for every non-empty line received from the backend.

    def read_loop(self, callback):
        while True:
            line = self.proc.stdout.readline()
            if not line:
                break
            callback(line.strip())  

## @brief Global ClientAPI instance used throughout the application.
api = ClientAPI()

## @brief Callback that processes all text responses from the backend API.
#
#  Routes each response to the appropriate UI action:
#  - Authentication results navigate to/from the login page.
#  - Emergency messages toggle the global emergency state.
#  - Taxi/landing clearances switch the active page.
#  - Flight-manual download results update @ref download_status and refresh
#    the PDF viewer.
#  - All messages are also echoed to the log panels on PreFlightPage and
#    ActiveAirspacePage.
#
#  @param text A single stripped line received from the backend.
def on_response(text):
    #Login ACK/NACK
    if text.startswith("Authentication Successful"):
        app.show_page("PreFlightPage")
    elif text.startswith("LoginAuth:") or text.startswith("Authentication Failed"):
        app.pages["LoginPage"].error_var.set(text)

    # Emergency ACK/resolve
    if text.startswith("Emergency acknowledged"):
        app.set_emergency(True)
    elif text.startswith("Emergency resolved"):
        app.set_emergency(False)

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

## @brief Sends an arbitrary command string to the backend API.
#  @param cmd The command string to transmit.
def send_cmd(cmd):
    api.send(cmd)


# ──────────────────────────────────────────────────────────────────────────────
# UI Components
# ──────────────────────────────────────────────────────────────────────────────
 
## @class Page
#  @brief Abstract base class for all application pages.
#
#  Inherits from tk.Frame and stores a reference to the top-level App
#  controller so that any page can trigger navigation or access shared state.
class Page(tk.Frame):
    ## @brief Constructs a Page frame inside the given parent container.
    #  @param parent   The parent Tk widget (typically the stacked container).
    #  @param controller The App instance that owns all pages.
    """Base class for all pages."""
    def __init__(self, parent, controller):
        super().__init__(parent)
        ## @brief Reference to the top-level App controller.
        self.controller = controller

## @class TopBar
#  @brief A toolbar widget shown at the top of every page.
#
#  Renders a left-side navigation button (Logout or Back, depending on the
#  page) and a right-side EMERGENCY toggle button that is shared across all
#  pages via the controller's @c emergency_buttons list.
class TopBar(ttk.Frame):
        ## @brief Constructs the top bar for a specific page.
    #  @param parent      The parent widget to attach this bar to.
    #  @param controller  The App controller instance.
    #  @param page_name   The name of the page this bar belongs to; controls
    #                     which left-side button is displayed.
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
        
        # Right: Emergency (toggles to END EMERGENCY when active)
        def on_emergency_click():
            if controller.emergency_active:
                controller.api.send("RESOLVE")
            else:
                controller.api.send("EMERGENCY")

        emergency_btn = ttk.Button(self, text="EMERGENCY", command=on_emergency_click)
        emergency_btn.pack(side="right", padx=5)
        controller.emergency_buttons.append(emergency_btn)


## @class LogPanel
#  @brief A scrollable, read-only text widget for displaying backend log messages.
class LogPanel(ttk.Frame):
    ## @brief Constructs the log panel inside the given parent widget.
    #  @param parent The parent widget to attach this panel to.
    def __init__(self, parent):
        super().__init__(parent)

    ## @brief The underlying Text widget (disabled except during updates).
        self.text = tk.Text(self, height=15, state="disabled")
        scroll = ttk.Scrollbar(self, command=self.text.yview)
        self.text.configure(yscrollcommand=scroll.set)

        self.text.pack(side="left", fill="both", expand=True, padx=10, pady=10)
        scroll.pack(side="right", fill="y")

    ## @brief Appends a message to the log and scrolls to the bottom.
    #  @param msg The message string to append.
    def add(self, msg):
        self.text.config(state="normal")
        self.text.insert("end", msg + "\n")
        self.text.see("end")
        self.text.config(state="disabled")

## @class LoginPage
#  @brief Page that collects pilot credentials and authenticates against the backend.
#
#  Displays username and password fields and sends a @c LOGIN command to the
#  backend API on submission.  Authentication responses are handled in
#  @ref on_response.
class LoginPage(Page):
    ## @brief Constructs the login page UI.
    #  @param parent     The parent container widget.
    #  @param controller The App controller instance.
    def __init__(self, parent, controller):
        super().__init__(parent, controller)

        outer = ttk.Frame(self)
        outer.pack(expand=True, padx=40)

        ttk.Label(outer, text="Login", font=("Arial", 20)).pack(pady=20)

        ## @brief StringVar bound to the username entry field.
        self.user_var = tk.StringVar()
        ## @brief StringVar bound to the password entry field.
        self.pass_var = tk.StringVar()
        ## @brief StringVar used to display authentication error messages.
        self.error_var = tk.StringVar()

        ttk.Entry(outer, textvariable=self.user_var, width=25).pack(pady=8)
        ttk.Entry(outer, textvariable=self.pass_var, show="*", width=25).pack(pady=8)

        ttk.Button(outer, text="Login", command=self.try_login).pack(pady=15)
        ttk.Label(outer, textvariable=self.error_var, foreground="red").pack()
## @brief Reads the username and password fields and sends a LOGIN command.
    def try_login(self):
        user = self.user_var.get()
        pw = self.pass_var.get()
        self.controller.api.send(f"LOGIN {user} {pw}")

## @class PreFlightPage
#  @brief Page shown after login for pre-flight operations.
#
#  Provides controls for requesting a flight plan, weather information,
#  taxi clearance, and access to the aircraft manual.  All action buttons
#  are disabled while an emergency is active.
class PreFlightPage(Page):
    ## @brief Constructs the pre-flight page UI.
    #  @param parent     The parent container widget.
    #  @param controller The App controller instance.
    def __init__(self, parent, controller):
        super().__init__(parent, controller)

        # Top bar
        TopBar(self, controller, page_name="PreFlightPage").pack(fill="x")

        ## @brief The log panel that displays backend messages for this page.
        self.log_panel = LogPanel(self)
        self.log_panel.pack(fill="both", expand=True, pady=10)

        # Button row
        btns = ttk.Frame(self)
        btns.pack(pady=10)

        ## @brief List of action buttons that are disabled during an emergency.
        self._action_buttons = [
            ttk.Button(btns, text="Flight Plan",    width=20, command=self.ask_flight),
            ttk.Button(btns, text="Weather",        width=20, command=self.ask_weather),
            ttk.Button(btns, text="Taxi Request",   width=20, command=lambda: controller.api.send("TAXI")),
            ttk.Button(btns, text="Aircraft Manual",width=20, command=lambda: controller.show_page("PDFViewerPage")),
        ]
        for btn in self._action_buttons:
            btn.pack(pady=5)

    ## @brief Enables or disables action buttons in response to an emergency state change.
    #  @param active True to enter emergency mode (buttons disabled); False to restore them.   
    def set_emergency_mode(self, active):
        state = "disabled" if active else "normal"
        for btn in self._action_buttons:
            btn.config(state=state)
        msg = "⚠ EMERGENCY ACTIVE — all requests locked" if active else "✓ Emergency cleared"
        self.log_panel.add(msg)

    ## @brief Sends a WEATHER request for a hard-coded airport code.
    #  @todo Replace with a popup that accepts a user-supplied airport code.
    def ask_weather(self):
        # TODO: popup for airport code
        self.controller.api.send("WEATHER YYZ")

    ## @brief Sends a FLIGHT request for a hard-coded flight ID.
    #  @todo Replace with a popup that accepts a user-supplied flight ID.
    def ask_flight(self):
        # TODO: popup for flight ID
        self.controller.api.send("FLIGHT AC123")

## @class ActiveAirspacePage
#  @brief Page displayed while the aircraft is in active airspace.
#
#  Provides controls for telemetry updates, air-traffic requests, runway
#  clearance, and access to the aircraft manual.  All action buttons are
#  disabled while an emergency is active
class ActiveAirspacePage(Page):
    ## @brief Constructs the active airspace page UI.
    #  @param parent     The parent container widget.
    #  @param controller The App controller instance.
    def __init__(self, parent, controller):
        super().__init__(parent, controller)

        TopBar(self, controller, page_name="ActiveAirspacePage").pack(fill="x")

        ## @brief The log panel that displays backend messages for this page.
        self.log_panel = LogPanel(self)
        self.log_panel.pack(fill="both", expand=True, pady=10)

        btns = ttk.Frame(self)
        btns.pack(pady=10)

        ## @brief List of action buttons that are disabled during an emergency.
        self._action_buttons = [
            ttk.Button(btns, text="Telemetry Update",   width=20, command=lambda: controller.api.send("TELEM")),
            ttk.Button(btns, text="Airtraffic Request", width=20, command=lambda: controller.api.send("TRAFFIC")),
            ttk.Button(btns, text="Clear Runway",       width=20, command=lambda: controller.api.send("TAXI")),
            ttk.Button(btns, text="Aircraft Manual",    width=20, command=lambda: controller.show_page("PDFViewerPage")),
        ]
        for btn in self._action_buttons:
            btn.pack(pady=5)

    ## @brief Enables or disables action buttons in response to an emergency state change.
    #  @param active True to enter emergency mode (buttons disabled); False to restore them.
    def set_emergency_mode(self, active):
        state = "disabled" if active else "normal"
        for btn in self._action_buttons:
            btn.config(state=state)
        msg = "⚠ EMERGENCY ACTIVE — all requests locked" if active else "✓ Emergency cleared"
        self.log_panel.add(msg)

## @class PDFViewerPage
#  @brief Page that downloads and displays the aircraft flight manual as a PDF.
#
#  If no manual has been downloaded yet, a Download button is shown.  Once
#  a download is in progress, an animated "Downloading..." spinner is
#  displayed.  After a successful download the PDFViewer widget is embedded
#  directly in the page.
class PDFViewerPage(Page):
    ## @brief Constructs the PDF viewer page UI.
    #  @param parent     The parent container widget.
    #  @param controller The App controller instance.
    def __init__(self, parent, controller):
        super().__init__(parent, controller)

        TopBar(self, controller, page_name="PDFViewerPage").pack(fill="x")

        ## @brief The inner frame whose children are swapped between states.
        self.display = ttk.Frame(self)
        self.display.pack(fill="both", expand=True)
        self.display.pack(fill="both", expand=True, padx=10, pady=10)
        
    
        ## @brief True while a download is in progress (prevents concurrent refreshes).
        self.loading = False
        ## @brief The animated label widget shown during a download, or None.
        self.loading_label = None
        ## @brief Current index into the dots animation sequence (0–2).
        self.loading_step = 0

        self.refresh()

    ## @brief Rebuilds the display area to reflect the current download state.
    #
    #  - If a download is in progress, returns immediately without changes.
    #  - If no file is available, renders the Download button.
    #  - If a file exists on disk, renders the PDFViewer widget.
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


    ## @brief Clears the display area and starts the animated loading indicator.
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

    ## @brief Advances the loading animation by one frame and schedules the next.
    #
    #  Cycles through "Downloading.", "Downloading..", "Downloading..." at
    #  500 ms intervals.  Stops automatically when @ref loading is False.
    def animate_loading(self):
        if not self.loading:
            return

        dots = ["Downloading.", "Downloading..", "Downloading..."]
        self.loading_label.config(text=dots[self.loading_step])

        self.loading_step = (self.loading_step + 1) % 3

        # Schedule next frame
        self.after(500, self.animate_loading)

    ## @brief Stops the loading animation and calls @ref refresh to update the display.
    def stop_loading(self):
        self.loading = False

        if self.loading_label:
            self.loading_label.destroy()
            self.loading_label = None

        self.refresh()

    ## @brief Starts the loading spinner and sends a MANUAL command to the backend.
    def download_manual(self):
        # Start spinner immediately
        self.start_loading()

        self.display.update_idletasks()  # ensure spinner shows before sending command

        # Send command to backend
        self.controller.api.send("MANUAL")

## @class App
#  @brief The root Tk window and central controller for the application.
#
#  Instantiates all pages, stacks them in a grid, manages page navigation,
#  and coordinates global state such as the emergency flag and the shared
#  list of emergency toggle buttons.
class App(tk.Tk):
    ## @brief Constructs the App window, creates all pages, and shows the login page.
    #  @param api A connected @ref ClientAPI instance used to communicate with the backend.
    def __init__(self, api):
        super().__init__()
        ## @brief The ClientAPI instance shared by all pages.
        self.api = api
        self.title("ClientApp UI")
        self.geometry("500x650")
        self.minsize(400, 600)

        self.eval('tk::PlaceWindow . center')  # Center the window on the screen

        container = tk.Frame(self)
        container.pack(fill="both", expand=True)
        container.grid_rowconfigure(0, weight=1)
        container.grid_columnconfigure(0, weight=1)
        ## @brief Dictionary mapping page class names to their instantiated Page objects.
        self.pages = {}
        ## @brief Name of the page that was active before the current one.
        self.previous_page = None
        ## @brief True when a declared emergency is in effect.        
        self.emergency_active = False
        ## @brief List of all EMERGENCY/END EMERGENCY buttons across all top bars.       
        self.emergency_buttons = []

        for P in (LoginPage, PreFlightPage, ActiveAirspacePage, PDFViewerPage):
            page = P(container, self)
            self.pages[P.__name__] = page
            page.grid(row=0, column=0, sticky="nsew")

        self.show_page("LoginPage")
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    ## @brief Sends a QUIT command to the backend and destroys the window.
    def _on_close(self):
        self.api.send("QUIT")
        self.destroy()


    ## @brief Updates the global emergency state and notifies all affected widgets.
    #
    #  Toggles the label on every registered emergency button and calls
    #  @c set_emergency_mode on PreFlightPage and ActiveAirspacePage.
    #
    #  @param active True to activate emergency mode; False to deactivate it.
    def set_emergency(self, active):
        self.emergency_active = active
        label = "END EMERGENCY" if active else "EMERGENCY"
        for btn in self.emergency_buttons:
            btn.config(text=label)
        for page_name in ("PreFlightPage", "ActiveAirspacePage"):
            self.pages[page_name].set_emergency_mode(active)

 
    ## @brief Raises the named page to the top of the widget stack.
    #
    #  Records the outgoing page in @ref previous_page so that the Back
    #  button on PDFViewerPage can return to the correct location.
    #
    #  @param name The key in @ref pages corresponding to the desired page.
    def show_page(self, name):
        if hasattr(self, "current_page"):
            self.previous_page = self.current_page

        self.current_page = name
        page = self.pages[name]
        page.tkraise()

## @brief Application entry point.
#
#  Creates the App, starts the backend read loop in a daemon thread, and
#  enters the Tkinter main event loop.
app = App(api)
threading.Thread(target=api.read_loop, args=(on_response,), daemon=True).start()
app.mainloop()

