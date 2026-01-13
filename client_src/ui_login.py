"""
ui_login.py

Modal "Connect & Login" dialog for the Tkinter client

Seminar work of "Fundamentals of Computer Networks"

"""

from __future__ import annotations

import tkinter as tk
from tkinter import messagebox, ttk


class LoginDialog:
    """
    Simple login/connect dialog
    
    Args:
        root: Tk root window
        on_ok: Callback invoked after validation
    """
    
    def __init__(self, root: tk.Tk, on_ok) -> None:
        """
        Create and display the dialog
        """
        self.root = root
        self.on_ok = on_ok

        self.win = tk.Toplevel(root)
        self.win.title("Login")
        self.win.transient(root)
        self.win.resizable(False, False)
        self.win.geometry("+200+200")

        self.win.lift()
        self.win.attributes("-topmost", True)
        self.win.after(200, lambda: self.win.attributes("-topmost", False))

        frm = ttk.Frame(self.win, padding = 14)
        frm.pack(fill = "both", expand = True)

        ttk.Label(frm, text = "Host / IP:").grid(row = 0, column = 0, sticky = "w")
        self.host = tk.StringVar(value = "127.0.0.1")
        ttk.Entry(frm, textvariable = self.host, width = 26).grid(row = 0, column = 1, sticky = "ew", pady = (0, 8))

        ttk.Label(frm, text = "Port:").grid(row = 1, column = 0, sticky = "w")
        self.port = tk.StringVar(value = "7777")
        ttk.Entry(frm, textvariable = self.port, width = 10).grid(row = 1, column = 1, sticky = "w", pady = (0, 8))

        ttk.Label(frm, text = "Nickname:").grid(row = 2, column = 0, sticky = "w")
        self.nick = tk.StringVar(value = "")
        ttk.Entry(frm, textvariable = self.nick, width = 16).grid(row = 2, column = 1, sticky = "w", pady = (0, 10))

        frm.columnconfigure(1, weight = 1)

        btns = ttk.Frame(frm)
        btns.grid(row = 3, column = 0, columnspan = 2, sticky = "e")
        ttk.Button(btns, text = "Quit", command = self._quit).pack(side = "right")
        ttk.Button(btns, text = "Connect & Login", style = "Primary.TButton", command = self._ok).pack(
            side = "right", padx = (0, 8)
        )

        self.win.bind("<Return>", lambda e: self._ok())
        self.win.protocol("WM_DELETE_WINDOW", self._quit)

    def close(self) -> None:
        """
        Close the dialog window if it still exists
        """
        if self.win.winfo_exists():
            self.win.destroy()

    def _quit(self) -> None:
        """
        Quit the entire application
        """
        self.root.destroy()

    def _ok(self) -> None:
        """
        Validate inputs and invoke the on_ok callback
        """
        host = self.host.get().strip()
        port_s = self.port.get().strip()
        nick = self.nick.get().strip()

        if not host:
            messagebox.showerror("Input", "Host must not be empty", parent = self.win)
            return

        try:
            port = int(port_s)
            if port < 1 or port > 65535:
                raise ValueError()
        except ValueError:
            messagebox.showerror("Input", "Port must be 1-65535", parent = self.win)
            return

        if not nick:
            messagebox.showerror("Input", "Nick must not be empty", parent = self.win)
            return

        self.on_ok(host, port, nick)
