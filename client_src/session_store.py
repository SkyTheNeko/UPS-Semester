"""
session_store.py

Persistent storage for client session tokens

Seminar work of "Fundamentals of Computer Networks"

"""

from __future__ import annotations
import json, os

SESSION_FILE = ".game_session.json"

def _load_all() -> dict[str, str]:
    """
    Load the entire session store from disk

    Returns:
        Dictionary mapping nicknames to session tokens
        Returns an empty dict if the file does not exist, is invalid, or cannot be read
    """
    if not os.path.exists(SESSION_FILE):
        return {}
    try:
        with open(SESSION_FILE, "r", encoding="utf-8") as f:
            obj = json.load(f)
        return obj if isinstance(obj, dict) else {}
    except Exception:
        return {}

def _save_all(d: dict[str, str]) -> None:
    """
    Write the entire session store to disk

    Args:
        d: Dictionary mapping nicknames to session tokens
    """
    try:
        with open(SESSION_FILE, "w", encoding="utf-8") as f:
            json.dump(d, f)
    except Exception:
        pass

def load_session_for(nick: str) -> str:
    """
    Load a stored session token for a specific nickname

    Args:
        nick: Player nickname

    Returns:
        Session token string if found, otherwise an empty string
    """
    if not nick:
        return ""
    return _load_all().get(nick, "")

def save_session_for(nick: str, session: str) -> None:
    """
    Save or remove a session token for a specific nickname

    Args:
        nick: Player nickname
        session: Session token to store
    """
    if not nick:
        return
    d = _load_all()
    if session:
        d[nick] = session
    else:
        d.pop(nick, None)
    _save_all(d)
