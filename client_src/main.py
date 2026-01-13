"""
main.py

Application entry point

Seminar work of "Fundamentals of Computer Networks"

"""

from __future__ import annotations

import tkinter as tk
from tkinter import ttk

from app import App

def main() -> None:
    """
    Initialize and run the GUI application
    """
    root = tk.Tk()

    style = ttk.Style()
    try:
        style.theme_use("clam")
    except tk.TclError:
        pass
    style.configure("Primary.TButton", padding=(12, 8))

    App(root)
    root.mainloop()


if __name__ == "__main__":
    main()
