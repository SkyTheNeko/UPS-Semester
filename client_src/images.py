"""
images.py

Card image loading and caching for the Tkinter client

Seminar work of "Fundamentals of Computer Networks"

"""

from __future__ import annotations

import os
import tkinter as tk

CARDS_DIR = "cards"

class CardImages:
    """
    Loader/cache for card images

    """
    def __init__(self) -> None:
        """
        Initialize an empty cache and no placeholder image
        """
        self.cache: dict[str, tk.PhotoImage] = {}   # In-memory map of image name
        self._missing: tk.PhotoImage | None = None  # Placeholder image used when a PNG is not found

    def get(self, code: str) -> tk.PhotoImage:
        """
        Return a PhotoImage for a given card code

        Args:
            code: Card token used as filename without extension
                  If empty or "-", the special "EMPTY" image is used

        Returns:
            tk.PhotoImage for the requested card or a placeholder if missing
        """
        if not code or code == "-":
            return self._load("EMPTY")
        return self._load(code)

    def _load(self, name: str) -> tk.PhotoImage:
        """
        Load an image by name from disk (or return cached result)

        Args:
            name: Base filename to load

        Returns:
            Cached tk.PhotoImage instance
        """
        if name in self.cache:
            return self.cache[name]

        path = os.path.join(CARDS_DIR, f"{name}.png")
        if os.path.exists(path):
            img = tk.PhotoImage(file=path)
            self.cache[name] = img
            return img

        if self._missing is None:
            ph = tk.PhotoImage(width=80, height=110)
            row = " ".join(["#111318"] * 80)
            for y in range(110):
                ph.put("{" + row + "}", to=(0, y))
            self._missing = ph

        self.cache[name] = self._missing
        return self._missing
