"""
model.py

Client-side state container for a simple text-protocol card game.

Seminar work of "Fundamentals of Computer Networks"

"""

from __future__ import annotations
from dataclasses import dataclass, field
from typing import Dict, List, Optional


@dataclass
class RoomInfo:
    """
    Summary information about a room/lobby as shown in the room list
    
    """
    room_id: int        # Unique numeric ID of the room
    name: str = ""      # Human-readable name of the room
    players: str = ""   # Player count or player listing as provided by the server
    state: str = ""     # Room state label provided by the server


@dataclass
class GameState:
    """
    Current in-room state

    """
    room_id: int = -1                                       # ID of the room the client considers itself in
    host: str = "-"                                         # Nickname of the current host
    players: List[str] = field(default_factory=list)        # Ordered list of known players for the current room
    online: Dict[str, bool] = field(default_factory=dict)   # Map nick - online flag as last reported by server
    status: str = ""                                        # Human-readable status string for UI
    paused: bool = False                                    # Whether the game is paused
    
    phase: str = "NONE"                             # High-level phase string
    top: str = "-"                                  # Top card on discard pile
    active_suit: str = "-"                          # Active suit token
    penalty: int = 0                                # Current penalty count
    turn: str = "-"                                 # Nickname of the player whose turn it is
    hand: List[str] = field(default_factory=list)   # Cards in the client's hand

    """
    deck: int = 32
    draw_ends_turn: int = 1
    stack7: int = 1
    ace_skip: int = 1
    queen_wish: int = 1
    """


@dataclass
class Model:
    """
    Top-level client state

    """
    connected: bool = False     # Whether the network layer considers the client connected
    nick: str = ""              # Client nickname
    session: str = ""           # Session token issued by server after successful LOGIN
    last_server_msg: str = ""   # Most recent server informational message
    last_error: str = ""        # Most recent error line received

    rooms: Dict[int, RoomInfo] = field(default_factory=dict)    # Room list cache, keyed by room_id
    game: GameState = field(default_factory=GameState)          # Current in-room state

    def reset_rooms(self) -> None:
        """
        Clear cached room list information
        """
        self.rooms.clear()

    def apply_line(self, line: str) -> None:
        """
        Apply one raw line from the server to the model

        Args:
            line: Raw server line
        """
        if line.startswith("EVT "):
            self._apply_evt(line)
        elif line.startswith("RESP "):
            self._apply_resp(line)
        elif line.startswith("ERR "):
            self.last_error = line

    def _apply_resp(self, line: str) -> None:
        """
        Handle a response line from the server.

        Args:
            line: Full response line beginning with "RESP "
        """
        parts = line.split()
        if len(parts) < 2:
            return
        cmd = parts[1]
        kv = _parse_kv(parts[2:])

        if cmd == "LOGIN" and kv.get("ok") == "1":
            ses = kv.get("session", "")
            if ses:
                self.session = ses

        if cmd == "LEAVE_ROOM" and kv.get("ok") == "1":
            self.game = GameState()
            return


    def _apply_evt(self, line: str) -> None:
        """
        Handle an event line from the server.

        Args:
            line: Full event line beginning with "EVT "
        """
        parts = line.split()
        if len(parts) < 2:
            return
        etype = parts[1]
        kv = _parse_kv(parts[2:])

        if etype == "SERVER":
            self.last_server_msg = kv.get("msg", "")
            return

        if etype == "ROOM":
            rid = _to_int(kv.get("id", "-1"))
            if rid >= 0:
                r = self.rooms.get(rid) or RoomInfo(room_id=rid)
                r.name = kv.get("name", r.name)
                r.players = kv.get("players", r.players)
                r.state = kv.get("state", r.state)
                self.rooms[rid] = r
            return

        if etype == "PLAYER_JOIN":
            n = kv.get("nick", "")
            if n:
                if n not in self.game.players:
                    self.game.players.append(n)
                self.game.online[n] = True
            return

        if etype == "PLAYER_LEAVE":
            n = kv.get("nick", "")
            if n and n in self.game.players:
                self.game.players = [x for x in self.game.players if x != n]
                self.game.online.pop(n, None)
            return

        if etype == "PLAYER_OFFLINE":
            n = kv.get("nick", "")
            if n:
                self.game.online[n] = False
                if n not in self.game.players:
                    self.game.players.append(n)
            return

        if etype == "PLAYER_ONLINE":
            n = kv.get("nick", "")
            if n:
                self.game.online[n] = True
                if n not in self.game.players:
                    self.game.players.append(n)
            return

        if etype == "HOST":
            self.game.host = kv.get("nick", self.game.host)
            return

        if etype == "STATE":
            self.game.room_id = _to_int(kv.get("room", str(self.game.room_id)))
            self.game.phase = kv.get("phase", self.game.phase)
            self.game.top = kv.get("top", self.game.top)
            self.game.active_suit = kv.get("active_suit", self.game.active_suit)
            self.game.penalty = _to_int(kv.get("penalty", str(self.game.penalty)))
            self.game.turn = kv.get("turn", self.game.turn)
            self.game.paused = kv.get("paused", "0") == "1"
            return

        if etype == "TOP":
            self.game.top = kv.get("card", self.game.top)
            self.game.active_suit = kv.get("active_suit", self.game.active_suit)
            self.game.penalty = _to_int(kv.get("penalty", str(self.game.penalty)))
            return

        if etype == "TURN":
            self.game.turn = kv.get("nick", self.game.turn)
            return

        if etype == "HAND":
            cards = kv.get("cards", "")
            self.game.hand = [c.strip() for c in cards.split(",") if c.strip()] if cards else []
            return
        
        if etype in ("WINNER", "GAME_OVER", "GAME_END"):
            winner = kv.get("nick") or kv.get("winner") or ""
            if winner:
                self.game.status = f"Winner: {winner}"
            else:
                self.game.status = "Game over"
            return
    
    def reset_game(self) -> None:
        self.game = GameState()


def _parse_kv(tokens: List[str]) -> Dict[str, str]:
    """
    Parse a list of space-separated 'k=v' tokens into a dictionary

    Args:
        tokens: List like ["id=1", "name=Lobby"]

    Returns:
        Dict mapping keys to raw string values
    """
    kv: Dict[str, str] = {}
    for t in tokens:
        if "=" in t:
            k, v = t.split("=", 1)
            kv[k] = v
    return kv


def _to_int(s: Optional[str]) -> int:
    """
    Convert a possibly missing numeric string to int safely

    Args:
        s: String to parse, may be None or non-numeric

    Returns:
        Parsed integer, or 0 if conversion fails
    """
    try:
        return int(s or "0")
    except ValueError:
        return 0
