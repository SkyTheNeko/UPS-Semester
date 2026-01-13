"""
net.py

TCP client for a line-oriented text protocol

Seminar work of "Fundamentals of Computer Networks"

"""

from __future__ import annotations
import socket
import threading
from typing import Callable, Optional

LineHandler = Callable[[str], None]
StatusHandler = Callable[[str], None]

class TcpClient:
    """
    Simple TCP client with line-based receive loop
    
    """
    def __init__(self) -> None:
        """
        Initialize disconnected client state and empty receive buffer
        """
        self.sock: Optional[socket.socket] = None           # Active socket, or None when disconnected
        self._rx_thread: Optional[threading.Thread] = None  # Background receive thread
        self._stop = threading.Event()                      # Signals RX thread to exit
        self._buf = bytearray()                             # Accumulates bytes until full lines are parsed

        self.on_line: Optional[LineHandler] = None          # Called with complete received protocol lines
        self.on_status: Optional[StatusHandler] = None      # Called with status transitions

    def connect(self, host: str, port: int, timeout: float = 5.0) -> None:
        """
        Connect to a TCP server and start the receive loop thread

        Args:
            host: Server hostname/IP address
            port: Server port
            timeout: Timeout (seconds) used only for the connect() operation

        Raises:
            OSError: For connection failures
        """
        self.close()

        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        s.connect((host, port))
        s.settimeout(None)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        self.sock = s
        self._stop.clear()

        self._rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self._rx_thread.start()

        self._status(f"CONNECTED {host}:{port}")

    def close(self) -> None:
        """
        Close the connection and stop the receive loop
        """
        self._stop.set()
        if self.sock:
            try:
                self.sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock = None

    def is_connected(self) -> bool:
        """
        Return whether the client currently has an open socket
        """
        return self.sock is not None

    def send_line(self, line: str) -> None:
        """
        Send one protocol line to the server
        
        Args:
            line: Text line without a trailing newline

        Raises:
            RuntimeError: If the client is not connected
            OSError: If sending fails at the socket level
        """
        if not self.sock:
            raise RuntimeError("not connected")
        data = (line + "\n").encode("utf-8")
        self.sock.sendall(data)

    def _status(self, msg: str) -> None:
        """
        Emit a status message via the on_status callback

        Args:
            msg: Status string
        """
        if self.on_status:
            self.on_status(msg)

    def _emit_line(self, line: str) -> None:
        """
        Emit a received protocol line via the on_line callback

        Args:
            line: Decoded line
        """
        if self.on_line:
            self.on_line(line)

    def _rx_loop(self) -> None:
        """
        Background receive loop
        """
        s = self.sock
        if not s:
            return
        try:
            while not self._stop.is_set():
                chunk = s.recv(4096)
                if not chunk:
                    break
                self._buf.extend(chunk)
                self._drain_lines()
        except OSError as e:
            self._status(f"RX_ERROR {e}")
        finally:
            self._status("DISCONNECTED")
            try:
                s.close()
            except OSError:
                pass
            if self.sock is s:
                self.sock = None

    def _drain_lines(self) -> None:
        """
        Extract and emit all complete lines currently in the receive buffer
        """
        while True:
            idx = self._buf.find(b"\n")
            if idx < 0:
                return
            raw = self._buf[:idx]
            del self._buf[:idx + 1]

            if raw.endswith(b"\r"):
                raw = raw[:-1]
            if not raw:
                continue

            line = raw.decode("utf-8", errors="replace")
            self._emit_line(line)
