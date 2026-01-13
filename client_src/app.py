"""
app.py

Tkinter application controller for the game client.

Seminar work of "Fundamentals of Computer Networks"

"""

from __future__ import annotations

import queue
import threading
import time
import tkinter as tk
from tkinter import messagebox, ttk

from model import Model
from net import TcpClient
from images import CardImages
from ui_login import LoginDialog
from ui_game import GameWindow
from session_store import load_session_for, save_session_for

class App(ttk.Frame):
    """
    Main GUI controller
    
    """
    RECONNECT_INTERVAL_MS = 5000
    MAX_RECONNECT_ATTEMPTS = 8
    
    def __init__(self, root: tk.Tk) -> None:
        """
        Create the application controller and build the UI

        Args:
            root: Tk root window
        """
        super().__init__(root)
        self.root = root

        self._last_host: str = ""
        self._last_port: int = 0

        self._reconnect_attempts: int = 0       # Number of reconnect attempts in the current outage window
        self._reconnect_started: float = 0.0    # Time when reconnect loop started
        self._reconnect_job: str | None = None  # Tkinter after() job id for reconnect tick
        self._closing: bool = False             # True when application is shutting down

        self._ping_job: str | None = None   # Tkinter after() job id for ping tick
        self._ping_interval_ms: int = 5000  # Interval between PINGs
        self._pong_timeout_ms: int = 15000  # Max allowed time without PONG before disconnect

        self._awaiting_pong: bool = False   # True after sending PING until matching PONG arrives
        self._last_ping_sent: float = 0.0   # monotonic() timestamp of the last sent PING

        self.root.protocol("WM_DELETE_WINDOW", self._on_app_close)

        self.model = Model()
        self.net = TcpClient()
        self.images = CardImages()

        self.q: "queue.Queue[tuple[str, str]]" = queue.Queue()
        self._want_auto_resume = False

        self._login_dialog: LoginDialog | None = None
        self._game_win: GameWindow | None = None

        self._ui_mode = "NOT_IN_ROOM"
        self._last_phase = "NONE"

        self._current_room_name: str = ""
        self._current_room_id: int = -1

        self._C: dict[str, str] = {}

        self._build_styles()
        self._build_ui()
        self._wire_net()

        self.after(50, self._poll_events)

        self.root.after(0, self._prompt_login_visible)


    def _build_styles(self) -> None:
        """
        Configure ttk theme and define all shared styles for the application
        """
        style = ttk.Style()
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass

        self._C = {
            "bg": "#15181d",
            "card": "#1c2027",
            "card2": "#181c22",
            "border": "#2a303a",
            "text": "#e6e8ee",
            "muted": "#a9b1bc",
            "primary": "#4f8cff",
            "primary_hover": "#6fa2ff",
            "success": "#3fb950",
            "danger": "#f85149",
            "warning": "#d29922",
        }
        C = self._C

        
        self.root.configure(bg = C["bg"])

        
        style.configure(
            ".",
            background = C["bg"],
            foreground = C["text"],
            bordercolor = C["border"],
            lightcolor = C["border"],
            darkcolor = C["border"],
            troughcolor = C["card2"],
            focuscolor = C["primary"],
            selectbackground = C["primary"],
            selectforeground = "white",
            fieldbackground = C["card"],
        )

        style.configure("App.TFrame", background = C["bg"])
        style.configure("Card.TFrame", background = C["card"], padding = 14)
        style.configure("Toolbar.TFrame", background = C["card2"], padding = (12, 10))


        style.configure("Header.TLabel", background = C["card"], foreground = C["text"], font = ("Segoe UI", 12, "bold"))
        style.configure("Sub.TLabel", background = C["card"], foreground = C["muted"], font = ("Segoe UI", 10))
        style.configure("ToolbarSub.TLabel", background = C["card2"], foreground = C["muted"], font = ("Segoe UI", 10))
        style.configure("Nick.TLabel", font = ("Segoe UI", 12, "bold"))

        
        style.configure(
            "Status.Ok.TLabel",
            background = C["success"],
            foreground = "#08110b",
            padding = (10, 4),
            font = ("Segoe UI", 9, "bold"),
        )
        style.configure(
            "Status.Bad.TLabel",
            background = C["danger"],
            foreground = "white",
            padding = (10, 4),
            font = ("Segoe UI", 9, "bold"),
        )

        
        style.configure(
            "TButton",
            padding = (12, 8),
            background = C["border"],
            foreground = C["text"],
            borderwidth = 0,
            focusthickness = 2,
            focuscolor = C["primary"],
        )
        style.map(
            "TButton",
            background = [("active", "#343c49"), ("disabled", C["border"])],
            foreground = [("disabled", "#6b7482")],
        )

        style.configure("Primary.TButton", background = C["primary"], foreground = "white")
        style.map(
            "Primary.TButton",
            background = [("active", C["primary_hover"]), ("disabled", C["border"])],
            foreground = [("disabled", "#6b7482")],
        )

        style.configure("Danger.TButton", background = C["danger"], foreground = "white")
        style.map(
            "Danger.TButton",
            background = [("active", "#ff6b63"), ("disabled", C["border"])],
            foreground = [("disabled", "#6b7482")],
        )

        
        style.configure(
            "TEntry",
            padding = (10, 6),
            fieldbackground = C["card"],
            foreground = C["text"],
            insertcolor = C["text"],
            bordercolor = C["border"],
        )
        style.map(
            "TEntry",
            fieldbackground = [("disabled", C["card2"]), ("readonly", C["card2"])],
            foreground = [("disabled", "#6b7482")],
        )

        style.configure(
            "TSpinbox",
            padding = (10, 6),
            fieldbackground = C["card"],
            foreground = C["text"],
            insertcolor = C["text"],
            bordercolor = C["border"],
        )

        style.configure(
            "TCombobox",
            padding = (10, 6),
            fieldbackground = C["card"],
            foreground = C["text"],
            bordercolor = C["border"],
        )

        
        style.configure("TLabelframe", background = C["card"], bordercolor = C["border"])
        style.configure("TLabelframe.Label", background = C["card"], foreground = C["muted"], font = ("Segoe UI", 10, "bold"))

        
        style.configure(
            "Treeview",
            background = C["card"],
            fieldbackground = C["card"],
            foreground = C["text"],
            rowheight = 28,
            bordercolor = C["border"],
            lightcolor = C["border"],
            darkcolor = C["border"],
        )
        style.configure(
            "Treeview.Heading",
            background = C["card2"],
            foreground = C["text"],
            relief = "flat",
            font = ("Segoe UI", 9, "bold"),
        )
        style.map(
            "Treeview",
            background = [("selected", C["primary"])],
            foreground = [("selected", "white")],
        )

        
        style.configure("Vertical.TScrollbar", background = C["card2"], troughcolor = C["card2"], bordercolor = C["border"])
        style.configure("Horizontal.TScrollbar", background = C["card2"], troughcolor = C["card2"], bordercolor = C["border"])


    def _build_ui(self) -> None:
        """
        Build the main window layout
        """
        self.configure(style = "App.TFrame")
        self.pack(fill = "both", expand = True)

        self.root.title("UPS - Prsi")
        self.root.geometry("1150x720")
        self.root.minsize(1020, 650)

        self.toolbar = ttk.Frame(self, style = "Toolbar.TFrame")
        self.toolbar.pack(fill = "x", ipady = 4)


        left = ttk.Frame(self.toolbar)
        left.pack(side = "left", fill = "x", expand = True)

        ttk.Label(left, text = "Nick:", style = "Sub.TLabel").pack(side = "left")
        self.user_text = ttk.Label(left, text = "", style = "Nick.TLabel")
        self.user_text.pack(side = "left", padx = (8, 0))


        right = ttk.Frame(self.toolbar, style = "Toolbar.TFrame")
        right.pack(side = "right")

        status_row = ttk.Frame(right, style = "Toolbar.TFrame")
        status_row.pack(side = "top", anchor = "e")

        self.status_pill = ttk.Label(status_row, text = "DISCONNECTED", style = "Status.Bad.TLabel")
        self.status_pill.pack(side = "right")

        self.status_text = ttk.Label(status_row, text = "NOT CONNECTED", style = "ToolbarSub.TLabel")
        self.status_text.pack(side = "right", padx = (0, 12))

        self.btn_connect = ttk.Button(
            right,
            text = "Reconnect",
            style = "Primary.TButton",
            command = self.on_connect_dialog,
        )
        self.btn_connect.pack(side = "top", anchor = "e", pady = (6, 0))

        
        self.body = ttk.Frame(self, style = "App.TFrame")
        self.body.pack(fill = "both", expand = True, padx = 10, pady = (0, 10))
        self.body.columnconfigure(0, weight = 1)
        self.body.rowconfigure(0, weight = 1)

        
        self.rooms_card=ttk.Frame(self.body, style = "Card.TFrame")
        self.rooms_card.grid(row = 0, column = 0, sticky = "nsew")

        ttk.Label(self.rooms_card, text = "Rooms", style = "Header.TLabel").pack(anchor = "w")

        rooms_toolbar = ttk.Frame(self.rooms_card, style = "Card.TFrame")
        rooms_toolbar.pack(fill = "x", pady = (8, 6))

        ttk.Button(rooms_toolbar, text = "Create room", style = "Primary.TButton", command = self.on_create_room).pack(side = "left")
        ttk.Button(rooms_toolbar, text = "Join selected", style = "Primary.TButton", command = self.on_join_selected).pack(side = "left", padx = (8, 0))
        ttk.Button(rooms_toolbar, text = "Refresh", command = self.on_list_rooms).pack(side = "left", padx = (18, 0))

        cols = ("id", "name", "players", "state")
        self.rooms = ttk.Treeview(self.rooms_card, columns = cols, show = "headings", height = 14)
        for c, w in zip(cols, (70, 260, 95, 90)):
            self.rooms.heading(c, text = c.upper())
            self.rooms.column(c, width = w, anchor = "w")
        self.rooms.pack(fill = "both", expand = True)

        
        self.room_card=ttk.Frame(self.body, style = "Card.TFrame")

        self.lbl_room_title = ttk.Label(self.room_card, text = "Room: -", style = "Header.TLabel")
        self.lbl_room_title.pack(anchor = "w")

        self.lbl_room_hint = ttk.Label(self.room_card, text = "", style = "Sub.TLabel")
        self.lbl_room_hint.pack(anchor = "w", pady = (6, 10))
        
        self.lbl_round_winner = ttk.Label(self.room_card, text = "", style = "Sub.TLabel")
        self.lbl_round_winner.pack(anchor = "w", pady = (4, 0))

        self.players_box = ttk.LabelFrame(self.room_card, text = "Players")
        self.players_box.pack(fill = "x", pady = (10, 0))
        self.players_lbl = ttk.Label(self.players_box, text = "(none)", style = "Sub.TLabel")
        self.players_lbl.pack(anchor = "w", padx = 8, pady = 8)

        self.room_actions = ttk.Frame(self, style = "Card.TFrame")

        self.btn_start = ttk.Button(self.room_actions, text = "Start game", style = "Primary.TButton", command = self.on_start_game)
        self.btn_leave = ttk.Button(self.room_actions, text = "Leave room", style = "Danger.TButton", command = self.on_leave_room)

        self.room_actions.columnconfigure(2, weight = 1)
        self.btn_start.grid(row = 0, column = 0, sticky = "w")
        self.btn_leave.grid(row = 0, column = 1, sticky = "w", padx = (10, 0))

        
        self.log_card=ttk.Frame(self, style = "Card.TFrame")
        self.log_card.pack(fill = "both", expand = False, padx = 10, pady = (10, 10))
        ttk.Label(self.log_card, text = "Log", style = "Header.TLabel").pack(anchor = "w")

        self.log = tk.Text(self.log_card, height = 8, wrap = "none")
        self.log.pack(fill = "both", expand = True, pady = (8, 0))
        self.log.configure(state = "disabled")

        
        C = self._C
        self.log.configure(
            bg = "#111318",
            fg = "#e6e6e6",
            insertbackground = "#e6e6e6",
            relief = "flat",
            highlightthickness = 1,
            highlightbackground = C["border"],
            highlightcolor = C["border"],
        )

        self._apply_ui_mode("NOT_IN_ROOM")

    def _wire_net(self) -> None:
        """
        Connect TcpClient callbacks to the internal event queue
        """
        self.net.on_status = lambda msg: self.q.put(("status", msg))
        self.net.on_line = lambda line: self.q.put(("line", line))


    def _prompt_login(self) -> None:
        """
        Show the login dialog if it is not already open
        """
        if self._login_dialog is not None and self._login_dialog.win.winfo_exists():
            return
        self._login_dialog = LoginDialog(self.root, self._connect_and_login)

    def on_connect_dialog(self) -> None:
        """
        Button handler: open the login/connect dialog
        """
        self._prompt_login()

    def _close_login_dialog(self) -> None:
        """
        Close the login dialog if it exists
        """
        if self._login_dialog is not None:
            self._login_dialog.close()
            self._login_dialog = None

    def _connect_and_login(self, host: str, port: int, nick: str) -> None:
        """
        Start connection attempt to the given server and prepare for auto-login

        Args:
            host: Server hostname/IP
            port: Server port
            nick: Player nickname
        """
        self.model.nick = nick
        self.user_text.configure(text = f"{nick}")
        self._want_auto_resume = True

        self._last_host = host
        self._last_port = port
        self._reconnect_attempts = 0
        self._reconnect_started = 0.0
        self._cancel_reconnect()

        self._append_log(f"[ui] connect requested {host}:{port} nick={nick}")

        def work() -> None:
            try:
                self.net.connect(host, port)
            except Exception as e:
                self.q.put(("status", f"RX_ERROR {e}"))
                self.q.put(("status", "DISCONNECTED"))

        threading.Thread(target = work, daemon = True).start()


    def _poll_events(self) -> None:
        """
        Tk-thread event pump
        """
        changed = False
        try:
            while True:
                kind, payload = self.q.get_nowait()
                if kind == "status":
                    self._handle_status(payload)
                else:
                    self._handle_line(payload)
                changed = True
        except queue.Empty:
            pass

        if changed:
            self._refresh_ui()

        self.after(50, self._poll_events)

    def _handle_status(self, msg: str) -> None:
        """
        Handle network status notifications

        Args:
            msg: Status string
        """
        if msg == "RECONNECT_SCHEDULE":
            self._schedule_reconnect()
            return

        if msg.startswith("RX_ERROR"):
            self.model.connected = False
            self._set_status("DISCONNECTED", msg)
            self._want_auto_resume = True
            
            self._stop_ping()
            self._awaiting_pong = False
            self._last_ping_sent = 0.0

            self._hard_reset_after_disconnect(msg)
            self._schedule_reconnect()
            self._append_log(f"[net] {msg}")
            return

        if msg.startswith("CONNECTED"):
            self.model.connected = True
            self._set_status("CONNECTED", msg)
            
            self._start_ping()

            self._cancel_reconnect()
            self._reconnect_attempts = 0
            self._reconnect_started = 0.0

            if self._want_auto_resume:
                self._want_auto_resume = False
                self._auto_login_or_resume()

        elif msg.startswith("DISCONNECTED"):
            self.model.connected = False
            self._set_status("DISCONNECTED", "Server disconnected")
            self._want_auto_resume = True
            
            self._stop_ping()
            self._awaiting_pong = False
            self._last_ping_sent = 0.0

            self._hard_reset_after_disconnect("Server disconnected")

            self._schedule_reconnect()

        else:
            self._set_status("DISCONNECTED", msg)
            if not self.net.is_connected():
                self._want_auto_resume = True
                self._schedule_reconnect()

        self._append_log(f"[net] {msg}")



    def _auto_login_or_resume(self) -> None:
        """
        Attempt to restore an existing session for the current nick
        """
        ses = load_session_for(self.model.nick)
        if ses:
            self.model.session = ses
            self._send(f"REQ RESUME nick={self.model.nick} session={ses}")
        else:
            self._send(f"REQ LOGIN nick={self.model.nick}")

    def _handle_line(self, line: str) -> None:
        """
        Handle an incoming protocol line from the server

        Args:
            line: Raw server protocol line
        """
        self._append_log(f"<< {line}")

        prev_phase = self.model.game.phase
        prev_room=getattr(self.model.game, "room_id", -1)

        self.model.apply_line(line)

        if line.startswith("ERR LOGIN") and "code=NICK_TAKEN" in line:
            ses = load_session_for(self.model.nick)
            if ses:
                self.model.session = ses
                self._send(f"REQ RESUME nick={self.model.nick} session={ses}")
            else:
                messagebox.showerror("Login", "Nickname is already in use!")
            return

        new_phase = self.model.game.phase
        new_room=getattr(self.model.game, "room_id", -1)

        if line.startswith("ERR RESUME"):
            if self.model.nick:
                save_session_for(self.model.nick, "")
            self.model.session = ""
            self._send(f"REQ LOGIN nick={self.model.nick}")
            return

        if line.startswith("RESP LOGIN") and "ok=1" in line and self.model.session and self.model.nick:
            save_session_for(self.model.nick, self.model.session)
            self.user_text.configure(text = f"{self.model.nick}")
            self._close_login_dialog()
            self.on_list_rooms()

        if line.startswith("RESP RESUME") and "ok=1" in line:
            self.user_text.configure(text = f"{self.model.nick}")
            self._close_login_dialog()

            g = self.model.game
            if g.room_id >= 0:
                self._current_room_id = g.room_id
                if g.room_id in self.model.rooms:
                    self._current_room_name = self.model.rooms[g.room_id].name
            else:
                self._current_room_id = -1
                self._current_room_name = ""
                self.on_list_rooms()

            self._refresh_ui()

        if line.startswith("RESP CREATE_ROOM") and "ok=1" in line:
            self.on_list_rooms()
        if line.startswith("RESP JOIN_ROOM") and "ok=1" in line:
            self.on_list_rooms()
        if line.startswith("RESP LEAVE_ROOM") and "ok=1" in line:
            self._current_room_id = -1
            self._current_room_name = ""
            self.on_list_rooms()
            
        if line.startswith("EVT GAME_END"):
            winner = None
            parts = line.split()
            for p in parts:
                if p.startswith("winner="):
                    winner = p.split("=", 1)[1]

            if winner:
                self.lbl_round_winner.configure(
                    text = f"Last round winner: {winner}"
                )

                messagebox.showinfo(
                    "Round finished",
                    f"{winner} won the round!"
                )
                
        if line.startswith("RESP PONG"):
            self._awaiting_pong = False
            
        if prev_room !=  new_room and new_room < 0:
            self._current_room_id = -1
            self._current_room_name = ""
            self.on_list_rooms()

        if prev_phase != "GAME" and new_phase == "GAME":
            self._ensure_game_window()
        if prev_phase == "GAME" and new_phase !=  "GAME":
            if self._game_win is not None and self._game_win.winfo_exists():
                self._game_win.destroy()
            self._game_win = None

        self._last_phase = new_phase

    def _send(self, line: str) -> None:
        """
        Send a protocol line to the server asynchronously
        
        Args:
            line: Full request line
        """
        def work() -> None:
            try:
                self.net.send_line(line)
                self.q.put(("line", f">> {line}"))
            except Exception as e:
                self.q.put(("status", f"RX_ERROR {e}"))

        threading.Thread(target = work, daemon = True).start()

    def on_disconnect(self) -> None:
        """
        Close the network connection immediately
        """
        self.net.close()

    def on_list_rooms(self) -> None:
        """
        Request the server's room list
        """
        if not self.net.is_connected():
            return
        self.model.reset_rooms()
        self._send("REQ LIST_ROOMS")

    def on_create_room(self) -> None:
        """
        Open a modal dialog to create a new room and send REQ CREATE_ROOM
        """
        if not self.net.is_connected():
            messagebox.showerror("Network", "Not connected")
            return

        dlg = tk.Toplevel(self.root)
        dlg.title("Create room")
        dlg.transient(self.root)
        dlg.grab_set()
        dlg.resizable(False, False)

        frm = ttk.Frame(dlg, padding = 12)
        frm.pack(fill = "both", expand = True)

        ttk.Label(frm, text = "Name:").grid(row = 0, column = 0, sticky = "w")
        name_var = tk.StringVar(value = "Room")
        ttk.Entry(frm, textvariable = name_var, width = 28).grid(row = 0, column = 1, sticky = "ew", pady = (0, 8))

        ttk.Label(frm, text = "Players (2-4):").grid(row = 1, column = 0, sticky = "w")
        size_var = tk.IntVar(value = 2)
        ttk.Spinbox(frm, from_ = 2, to = 4, textvariable = size_var, width = 6).grid(row = 1, column = 1, sticky = "w", pady = (0, 8))

        def ok() -> None:
            name = name_var.get().strip()
            if not name:
                messagebox.showerror("Input", "Name must not be empty", parent = dlg)
                return
            size = int(size_var.get())
            if size < 2 or size > 4:
                messagebox.showerror("Input", "Size must be 2-4", parent = dlg)
                return

            self._current_room_name = name
            self._send(f"REQ CREATE_ROOM name={name} size={size}")
            dlg.destroy()

        btns = ttk.Frame(frm)
        btns.grid(row = 2, column = 0, columnspan = 2, sticky = "e", pady = (10, 0))
        ttk.Button(btns, text = "Cancel", command = dlg.destroy).pack(side = "right")
        ttk.Button(btns, text = "Create", style = "Primary.TButton", command = ok).pack(side = "right", padx = (0, 8))

    def on_join_selected(self) -> None:
        """
        Join the currently selected room in the Treeview
        """
        if not self.net.is_connected():
            messagebox.showerror("Network", "Not connected")
            return
        sel = self.rooms.selection()
        if not sel:
            messagebox.showinfo("Join", "Select a room first")
            return
        vals = self.rooms.item(sel[0], "values")
        rid = int(vals[0])
        rname = str(vals[1])

        self._current_room_id = rid
        self._current_room_name = rname

        self._send(f"REQ JOIN_ROOM room={rid}")

    def on_leave_room(self) -> None:
        """
        Send REQ LEAVE_ROOM
        """
        if not self.net.is_connected():
            return
        self._send("REQ LEAVE_ROOM")

    def on_start_game(self) -> None:
        """
        Start the game in the current room
        """
        if not self.net.is_connected():
            return
        self.lbl_round_winner.configure(text = "")
        self._send("REQ START_GAME")

    def on_draw(self) -> None:
        """
        Request to draw a card
        """
        if not self.net.is_connected():
            return
        if self.model.game.phase !=  "GAME":
            messagebox.showinfo("Draw", "No game is running.")
            return
        self._send("REQ DRAW")

    def on_play_card(self, card: str) -> None:
        """
        Attempt to play a card.
        
        Args:
            card: Two-character card token
        """
        if not self.net.is_connected():
            return

        if self.model.game.phase != "GAME":
            messagebox.showinfo("Move", "No game is running.")
            return

        if self.model.game.turn not in ("-", self.model.nick):
            messagebox.showinfo("Move", "Not your turn")
            return

        if len(card) == 2 and card[1] == "Q":
            suit = self._ask_suit()
            if not suit:
                return
            self._send(f"REQ PLAY card={card} wish={suit}")
            return

        self._send(f"REQ PLAY card={card}")


    def _ask_suit(self) -> str:
        """
        Modal dialog to choose a wished suit for Queen plays

        Returns:
            One-letter suit code: "H", "D", "C", "S"
            Returns "" if dialog is closed/cancelled
        """
        dlg = tk.Toplevel(self.root)
        dlg.title("Wish suit")
        dlg.transient(self.root)
        dlg.grab_set()
        dlg.resizable(False, False)

        v = tk.StringVar(value = "H")

        frm = ttk.Frame(dlg, padding = 12)
        frm.pack(fill = "both", expand = True)

        ttk.Label(frm, text = "Choose suit:").pack(anchor = "w")
        row = ttk.Frame(frm)
        row.pack(pady = 10, anchor = "w")

        for s, txt in [("H", "Hearts"), ("D", "Diamonds"), ("C", "Clubs"), ("S", "Spades")]:
            ttk.Radiobutton(row, text = txt, variable = v, value = s).pack(anchor = "w")

        res = {"val": ""}

        def ok() -> None:
            res["val"] = v.get()
            dlg.destroy()

        def cancel() -> None:
            dlg.destroy()

        btns = ttk.Frame(frm)
        btns.pack(fill = "x", pady = (10, 0))
        ttk.Button(btns, text = "Cancel", command = cancel).pack(side = "right")
        ttk.Button(btns, text = "OK", style = "Primary.TButton", command = ok).pack(side = "right", padx = (0, 8))

        self.root.wait_window(dlg)
        return res["val"]


    def _compute_ui_mode(self) -> str:
        """
        Compute the desired UI mode based on current Model/GameState

        Returns:
            "NOT_IN_ROOM"     if room_id < 0 or phase == "NONE"
            "IN_GAME"         if phase == "GAME"
            "IN_ROOM_LOBBY"   otherwise
        """
        g = self.model.game
        room_id = getattr(g, "room_id", -1)
        phase = g.phase

        if room_id < 0 or phase == "NONE":
            return "NOT_IN_ROOM"
        if phase == "GAME":
            return "IN_GAME"
        return "IN_ROOM_LOBBY"

    def _apply_ui_mode(self, mode: str) -> None:
        """
        Switch the visible view according to the given mode

        Args:
            mode: UI mode string
        """
        if mode == self._ui_mode:
            return
        self._ui_mode = mode

        if mode == "NOT_IN_ROOM":
            self._hide_room_actions()
            self.room_card.grid_remove()
            self.rooms_card.grid()
            self.rooms_card.grid_configure(row = 0, column = 0, sticky = "nsew")
        else:
            self._show_room_actions()
            self.rooms_card.grid_remove()
            self.room_card.grid()
            self.room_card.grid_configure(row = 0, column = 0, sticky = "nsew")

    def _refresh_ui(self) -> None:
        """
        Re-render the UI from the current Model state
        """
        self.user_text.configure(text = f"{self.model.nick or '-'}")

        mode = self._compute_ui_mode()

        if mode == "NOT_IN_ROOM":
            self._hide_room_actions()
        else:
            self._show_room_actions()

        self._apply_ui_mode(mode)

        if mode == "NOT_IN_ROOM":
            self._refresh_rooms()
        else:
            self._refresh_room_panel()

        if self._game_win is not None and self._game_win.winfo_exists():
            self._game_win.refresh_from_model()
        elif self.model.game.phase == "GAME":
            self._ensure_game_window()

    def _refresh_rooms(self) -> None:
        """
        Rebuild the Treeview rows
        """
        for i in self.rooms.get_children():
            self.rooms.delete(i)
        for rid in sorted(self.model.rooms.keys()):
            r = self.model.rooms[rid]
            self.rooms.insert("", "end", values = (r.room_id, r.name, r.players, r.state))

    def _format_players_line(self) -> str:
        """
        Format one-line players status for the room panel

        Returns:
            Formatted string or "(none)"
        """
        g = self.model.game
        host = getattr(g, "host", "-") or "-"
        if not g.players:
            return "(none)"

        items: list[str] = []
        for p in g.players:
            online = g.online.get(p, True)
            st = "ONLINE" if online else "OFFLINE"
            tag = " (HOST)" if (host != "-" and p == host) else ""
            you = " (YOU)" if p == self.model.nick else ""
            items.append(f"{p}{you}{tag} [{st}]")
        return "  |  ".join(items)

    def _refresh_room_panel(self) -> None:
        """
        Refresh the room panel
        """
        g = self.model.game

        if g.room_id >= 0:
            self._current_room_id = g.room_id
            if not self._current_room_name and g.room_id in self.model.rooms:
                self._current_room_name = self.model.rooms[g.room_id].name

        room_name = self._current_room_name or (f"#{g.room_id}" if g.room_id >= 0 else "-")
        self.lbl_room_title.configure(text = f"Room: {room_name}")

        if g.phase == "GAME":
            self.lbl_room_hint.configure(text = "Game running in separate window.")
        else:
            self.lbl_room_hint.configure(text = "Waiting in room lobby.")

        self.players_lbl.configure(text = self._format_players_line())

        self.btn_leave.configure(state = ("normal" if self.model.connected else "disabled"))

        host = getattr(g, "host", "-") or "-"
        is_host = bool(self.model.nick) and (host == self.model.nick)
        can_start = self.model.connected and (g.phase == "LOBBY") and is_host
        self.btn_start.configure(state = ("normal" if can_start else "disabled"))

    def _ensure_game_window(self) -> None:
        """
        Ensure GameWindow exists and is synced with the current model
        """
        if self._game_win is not None and self._game_win.winfo_exists():
            return
        self._game_win = GameWindow(self.root, self)
        self._game_win.refresh_from_model()


    def _append_log(self, text: str) -> None:
        """
        Append one line to the log Text widget
        """
        self.log.configure(state = "normal")
        self.log.insert("end", text + "\n")
        self.log.see("end")
        self.log.configure(state = "disabled")

    def _set_status(self, pill: str, text: str) -> None:
        """
        Update the toolbar connection status

        Args:
            pill: "CONNECTED" or anything else treated as disconnected
            text: Detail string shown next to the pill
        """
        self.status_text.configure(text = text)
        if pill == "CONNECTED":
            self.status_pill.configure(text = "CONNECTED", style = "Status.Ok.TLabel")
        else:
            self.status_pill.configure(text = "DISCONNECTED", style = "Status.Bad.TLabel")

    def _prompt_login_visible(self) -> None:
        """
        Bring the root window to front and show the login dialog
        """
        self.root.deiconify()
        self.root.update_idletasks()
        self.root.lift()
        try:
            self.root.attributes("-topmost", True)
            self.root.after(200, lambda: self.root.attributes("-topmost", False))
        except Exception:
            pass
        self._prompt_login()

    def _is_card_playable(self, card: str) -> bool:
        """
        Client-side predicate for whether a card is playable right now

        Args:
            card: Two-character card token

        Returns:
            True if the card is playable under the local view of rules/state
        """
        g = self.model.game

        if g.phase != "GAME":
            return False
        if g.turn not in ("-", self.model.nick):
            return False

        if len(card) != 2:
            return False

        suit = card[0]
        rank = card[1]

        if g.penalty > 0:
            return rank == "7"

        if rank == "Q":
            return True

        top = g.top or "-"
        top_rank = top[1] if len(top) == 2 else ""
        return (suit == g.active_suit) or (rank == top_rank)

    def _show_room_actions(self) -> None:
        """
        Show the start/leave buttons
        """
        if not self.room_actions.winfo_ismapped():
            self.room_actions.pack(before = self.log_card, fill = "x", padx = 10, pady = (0, 6))

    def _hide_room_actions(self) -> None:
        """
        Hide the start/leave buttons
        """
        if self.room_actions.winfo_ismapped():
            self.room_actions.pack_forget()

    def _on_app_close(self) -> None:
        """
        Handle application close
        """
        if self._closing:
            return
        self._closing = True
        try:
            if self.net.is_connected():
                try:
                    self.net.send_line("REQ LEAVE_ROOM")
                except Exception:
                    pass
        finally:
            try:
                self.net.close()
            except Exception:
                pass
            self._cancel_reconnect()
            self.root.destroy()

    def _cancel_reconnect(self) -> None:
        """
        Cancel any scheduled auto-reconnect job
        """
        if self._reconnect_job is not None:
            try:
                self.after_cancel(self._reconnect_job)
            except Exception:
                pass
            self._reconnect_job = None

    def _schedule_reconnect(self) -> None:
        """
        Schedule an auto-reconnect attempt if possible
        """
        if self._closing:
            return
        if not self._last_host or not self._last_port:
            return
        if self.net.is_connected():
            return

        if self._reconnect_attempts >= self.MAX_RECONNECT_ATTEMPTS:
            self._set_status("DISCONNECTED", "Server unavailable - click Reconnect")
            self._cancel_reconnect()
            return

        self._cancel_reconnect()
        self._reconnect_job = self.after(self.RECONNECT_INTERVAL_MS, self._try_reconnect)

    def _try_reconnect(self) -> None:
        """
        Perform one auto-reconnect attempt
        """
        if self._closing or self.net.is_connected():
            return
        if not self._last_host or not self._last_port:
            return

        self._reconnect_attempts +=  1
        self._append_log(f"[ui] Auto-reconnect attempt {self._reconnect_attempts}/{self.MAX_RECONNECT_ATTEMPTS} to {self._last_host}:{self._last_port}")

        def work() -> None:
            try:
                self.net.connect(self._last_host, self._last_port)
            except Exception as e:
                self.q.put(("status", f"RX_ERROR {e}"))
                self.q.put(("status", "DISCONNECTED"))

        threading.Thread(target = work, daemon = True).start()

        
    def _hard_reset_after_disconnect(self, reason: str) -> None:
        """
        Clear UI + model state after a disconnect
        
        Args:
            reason: Formatted reason string
        """
        self.model.reset_game()
        self.model.reset_rooms()

        self._current_room_id = -1
        self._current_room_name = ""
        self.lbl_round_winner.configure(text = "")

        if self._game_win is not None and self._game_win.winfo_exists():
            self._game_win.destroy()
        self._game_win = None

        self._append_log(f"[ui] game stopped: {reason}")
        
    def _start_ping(self) -> None:
        """
        Start the client-side keep-alive ping loop
    
        """
        self._stop_ping()
        self._awaiting_pong = False
        self._last_ping_sent = 0.0
        self._ping_job = self.after(int(self._ping_interval_ms), self._ping_tick)

    def _stop_ping(self) -> None:
        """
        Stop the client-side ping loop
        """
        if self._ping_job is not None:
            try:
                self.after_cancel(self._ping_job)
            except Exception:
                pass
            self._ping_job = None

    def _ping_tick(self) -> None:
        """
        Periodic ping tick
        """
        if self._closing:
            return

        if not self.net.is_connected():
            self._stop_ping()
            return

        now = time.monotonic()

        if self._awaiting_pong and (now - self._last_ping_sent) > self._pong_timeout_ms:
            self._append_log("[net] PONG timeout - forcing disconnect")
            try:
                self.net.close()
            except Exception:
                pass
            self.q.put(("status", "DISCONNECTED"))
            return

        self._awaiting_pong = True
        self._last_ping_sent = now

        self._send("REQ PING")

        self._ping_job = self.after(int(self._ping_interval_ms), self._ping_tick)
