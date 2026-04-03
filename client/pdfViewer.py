# pdfViewer.py
import tkinter as tk
from tkinter import ttk
import fitz  # PyMuPDF
from PIL import Image, ImageTk
import io

class PDFViewer(ttk.Frame):
    """
    A pure-Python PDF viewer using PyMuPDF (fitz).
    If not installed: pip install PyMuPDF
    """

    def __init__(self, parent, pdf_path, **kwargs):
        super().__init__(parent, **kwargs)

        # Scrollable canvas
        self.canvas = tk.Canvas(self)
        self.scrollbar = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.canvas.configure(yscrollcommand=self.scrollbar.set)

        self.canvas.pack(side="left", fill="both", expand=True)
        self.scrollbar.pack(side="right", fill="y")

        # Internal frame
        self.inner = ttk.Frame(self.canvas)
        self.canvas.create_window((0, 0), window=self.inner, anchor="nw")

        # Load PDF pages
        self._load_pdf(pdf_path)

        # Update scroll region
        self.inner.bind("<Configure>", lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all")))

    def _load_pdf(self, pdf_path):
        """Render all PDF pages and display them as images."""
        doc = fitz.open(pdf_path)

        self.images = []  # keep references so Tk doesn't garbage collect them

        for page in doc:
            pix = page.get_pixmap()
            img_data = pix.tobytes("png")
            pil_img = Image.open(io.BytesIO(img_data))
            tk_img = ImageTk.PhotoImage(pil_img)

            self.images.append(tk_img)
            tk.Label(self.inner, image=tk_img).pack(pady=5)
