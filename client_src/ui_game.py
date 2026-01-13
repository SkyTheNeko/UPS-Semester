"""
ui_game.py

Game window UI (separate Toplevel) for the active match

Seminar work of "Fundamentals of Computer Networks"

"""

from __future__ import annotations

from typing import TYPE_CHECKING
import tkinter as tk
from tkinter import ttk

if TYPE_CHECKING:
    from app import App

class GameWindow(tk.Toplevel):
    """
    Separate window for the actual game
    
    Args:
        root: Tk root window
        app: Main application controller; used for accessing Model, images, and issuing actions
    """

    def __init__(self, root: tk.Tk, app: "App") -> None:
        """
        Build the game UI.
        
        """
        super().__init__(root)
        self.app = app

        self.title("UPS - Prsi")
        self.geometry("980x720")
        self.minsize(820, 620)

        self._img_refs: list[tk.PhotoImage] = []

        self.protocol("WM_DELETE_WINDOW", self._on_close)

        outer = ttk.Frame(self, padding = 12)
        outer.pack(fill = "both", expand = True)
        outer.columnconfigure(0, weight = 1)
        outer.rowconfigure(2, weight = 1)

        hdr = ttk.Frame(outer)
        hdr.grid(row = 0, column = 0, sticky = "ew")
        hdr.columnconfigure(1, weight = 1)

        self.lbl_room = ttk.Label(hdr, text = "Room: -", style = "Header.TLabel")
        self.lbl_room.grid(row = 0, column = 0, sticky = "w")

        self.lbl_status = ttk.Label(hdr, text = "", style = "Sub.TLabel")
        self.lbl_status.grid(row = 0, column = 1, sticky = "e")

        top = ttk.Frame(outer)
        top.grid(row = 1, column = 0, sticky = "ew", pady = (10, 10))
        top.columnconfigure(1, weight = 1)

        self.top_img = ttk.Label(top)
        self.top_img.grid(row = 0, column = 0, rowspan = 2, sticky = "w", padx = (0, 14))

        info = ttk.Frame(top)
        info.grid(row = 0, column = 1, sticky = "ew")
        info.columnconfigure(0, weight = 1)

        self.lbl_pen = ttk.Label(info, text = "7-Penalty: 0", style = "Sub.TLabel")
        self.lbl_turn = ttk.Label(info, text = "Turn: -", style = "Sub.TLabel")

        self.lbl_pen.grid(row = 0, column = 0, sticky = "w")
        self.lbl_turn.grid(row = 1, column = 0, sticky = "w", pady = (6, 0))

        self.players_box = ttk.LabelFrame(outer, text = "Players")
        self.players_box.grid(row = 2, column = 0, sticky = "ew")
        self.players_box.columnconfigure(0, weight = 1)
        self.players_lbl = ttk.Label(self.players_box, text = "(none)", style = "Sub.TLabel")
        self.players_lbl.grid(row = 0, column = 0, sticky = "w", padx = 8, pady = 6)

        lower = ttk.Frame(outer)
        lower.grid(row = 3, column = 0, sticky = "nsew", pady = (10, 0))
        lower.columnconfigure(0, weight = 1)
        lower.rowconfigure(0, weight = 1)

        self.hand_box = ttk.LabelFrame(lower, text = "Your hand")
        self.hand_box.grid(row = 0, column = 0, sticky = "nsew")
        self.hand_box.columnconfigure(0, weight = 1)
        self.hand_box.rowconfigure(0, weight = 1)

        self.hand_canvas = tk.Canvas(self.hand_box, highlightthickness = 0, bg = "#111318")
        self.hand_scroll_y = ttk.Scrollbar(self.hand_box, orient = "vertical", command = self.hand_canvas.yview)
        self.hand_frame = ttk.Frame(self.hand_canvas)

        self.hand_canvas.configure(yscrollcommand = self.hand_scroll_y.set)
        self.hand_canvas.create_window((0, 0), window = self.hand_frame, anchor = "nw")
        self.hand_frame.bind("<Configure>", lambda e: self.hand_canvas.configure(scrollregion = self.hand_canvas.bbox("all")),)

        self.hand_canvas.grid(row = 0, column = 0, sticky = "nsew")
        self.hand_scroll_y.grid(row = 0, column = 1, sticky = "ns")

        acts = ttk.Frame(lower)
        acts.grid(row = 1, column = 0, sticky = "ew", pady = (10, 0))
        acts.columnconfigure(3, weight = 1)

        self.btn_draw = ttk.Button(acts, text = "Draw", command = self.app.on_draw)
        self.btn_leave = ttk.Button(acts, text = "Leave room", command = self.app.on_leave_room)

        self.btn_draw.grid(row = 0, column = 0, sticky = "w")
        self.btn_leave.grid(row = 0, column = 1, sticky = "w", padx = (10, 0))

    def _on_close(self) -> None:
        """
        Handle window close
        """
        try:
            self.app.on_leave_room()
        finally:
            self.destroy()

    def refresh_from_model(self) -> None:
        """
        Re-render the game UI from the current Model/GameState
        """
        g = self.app.model.game
        self._img_refs.clear()

        room_name = self.app._current_room_name or (f"#{g.room_id}" if g.room_id >= 0 else "-")
        self.lbl_room.configure(text = f"Room: {room_name}")

        self.lbl_status.configure(text = getattr(g, "status", "") or "")

        self.lbl_pen.configure(text = f"7-Penalty: {g.penalty}")

        if g.phase == "GAME" and g.turn == self.app.model.nick:
            self.lbl_turn.configure(text = f"Turn: {g.turn} (YOU)", style = "Status.Ok.TLabel")
        else:
            self.lbl_turn.configure(text = f"Turn: {g.turn}", style = "Sub.TLabel")

        img = self.app.images.get(g.top)
        self._img_refs.append(img)
        self.top_img.configure(image = img)

        host = getattr(g, "host", "-") or "-"
        if not g.players:
            self.players_lbl.configure(text = "(none)")
        else:
            items: list[str] = []
            for p in g.players:
                online = g.online.get(p, True)
                st = "online" if online else "OFFLINE"
                tag = " (HOST)" if (host != "-" and p == host) else ""
                you = " (YOU)" if p == self.app.model.nick else ""
                items.append(f"{p}{you}{tag} [{st}]")
            self.players_lbl.configure(text = "  |  ".join(items))

        self.btn_draw.configure(state = ("normal" if (self.app.model.connected and g.phase == "GAME") else "disabled"))
        self.btn_leave.configure(state = ("normal" if self.app.model.connected else "disabled"))

        for w in self.hand_frame.winfo_children():
            w.destroy()

        cards = g.hand
        if not cards:
            ttk.Label(self.hand_frame, text = "(empty)", style = "Sub.TLabel").grid(
                row = 0, column = 0, sticky = "w", padx = 8, pady = 8
            )
            return

        canvas_w = self.hand_canvas.winfo_width()
        if canvas_w < 50:
            self.after(50, self.refresh_from_model)
            return

        tile_w = 115
        cols = max(1, canvas_w // tile_w)

        for i, c in enumerate(cards):
            r = i // cols
            k = i % cols
            im = self.app.images.get(c)
            self._img_refs.append(im)
            playable = self.app._is_card_playable(c)
            b = ttk.Button(
                self.hand_frame,
                image = im,
                command = (lambda cc = c: self.app.on_play_card(cc)) if playable else None,
                state = ("normal" if playable else "disabled"),
            )
            b.grid(row = r, column = k, padx = 8, pady = 8, sticky = "nsew")

        for k in range(cols):
            self.hand_frame.columnconfigure(k, weight = 1)
