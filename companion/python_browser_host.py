"""Python CEF host using cefpython3.
Implements off-screen rendering and socket communication compatible with existing C++ protocol.
"""
import sys
import os
import pathlib
import socket
import threading
import struct
import logging
from cefpython3 import cef

log = logging.getLogger("python_browser_host")
log.setLevel(logging.INFO)

# Configuration
FRAME_PORT = int(os.getenv("FRAME_PORT", "27015"))
CMD_PORT = int(os.getenv("CMD_PORT", "27016"))
HOST = "127.0.0.1"

# Global state
frame_clients = []  # list of (conn, addr)
cmd_clients = []
frame_lock = threading.Lock()

class RenderHandler(object):
    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.stride = width * 4
        self.buffer = bytearray(self.stride * height)

    def GetViewRect(self, rect_out):
        rect_out["x"] = 0
        rect_out["y"] = 0
        rect_out["width"] = self.width
        rect_out["height"] = self.height
        return True

    def OnPaint(self, element_type, dirty_rects, buffer, width, height):
        # Copy buffer (BGRA) to our bytearray and broadcast to frame clients
        if element_type != cef.PET_VIEW:
            return
        # buffer is a memoryview; copy directly
        self.buffer[:] = buffer[:]
        # Build frame header (same as C++ FrameHeader)
        magic = 0x48545250  # "HTRP"
        version = 1
        timestamp = int(threading.get_ident())  # placeholder
        header = struct.pack("<IIQIIII", magic, version, timestamp, width, height, self.stride, len(self.buffer))
        payload = header + self.buffer
        with frame_lock:
            for conn, _ in frame_clients:
                try:
                    conn.sendall(payload)
                except Exception as e:
                    log.error("Frame client send failed: %s", e)
                    # remove broken client
                    frame_clients.remove((conn, _))

def start_frame_server(render_handler):
    def client_thread(conn, addr):
        log.info("Frame client connected: %s:%s", *addr)
        with frame_lock:
            frame_clients.append((conn, addr))
        try:
            while True:
                # keep connection open; data sent from OnPaint
                data = conn.recv(1)
                if not data:
                    break
        finally:
            with frame_lock:
                if (conn, addr) in frame_clients:
                    frame_clients.remove((conn, addr))
            conn.close()
            log.info("Frame client disconnected: %s:%s", *addr)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, FRAME_PORT))
    srv.listen()
    log.info("Frame server listening on %s:%s", HOST, FRAME_PORT)
    while True:
        conn, addr = srv.accept()
        threading.Thread(target=client_thread, args=(conn, addr), daemon=True).start()

def start_cmd_server(browser):
    def handle_cmd(conn, addr):
        log.info("Cmd client connected: %s:%s", *addr)
        with conn.makefile('rwb') as f:
            while True:
                line = f.readline()
                if not line:
                    break
                line = line.decode('utf-8').strip()
                parts = line.split('\t')
                cmd = parts[0].upper()
                args = parts[1:] if len(parts) > 1 else []
                if cmd == "OPEN":
                    url = args[0] if args else "about:blank"
                    browser.LoadUrl(url)
                elif cmd == "LOAD":
                    url = args[0] if args else "about:blank"
                    browser.LoadUrl(url)
                elif cmd == "JS":
                    js = args[0] if args else ""
                    browser.ExecuteJavascript(js)
                elif cmd == "CLOSE":
                    browser.CloseBrowser(True)
                elif cmd == "STOP":
                    break
                else:
                    log.warning("Unknown command: %s", line)
        conn.close()
        log.info("Cmd client disconnected: %s:%s", *addr)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, CMD_PORT))
    srv.listen()
    log.info("Cmd server listening on %s:%s", HOST, CMD_PORT)
    while True:
        conn, addr = srv.accept()
        threading.Thread(target=handle_cmd, args=(conn, addr), daemon=True).start()

def main():
    # Initialize CEF
    settings = {
        "windowless_rendering_enabled": True,
        "log_severity": cef.LOGSEVERITY_INFO,
        "log_file": "python_browser_host.log",
        "browser_subprocess_path": sys.executable,
    }
    cef.Initialize(settings)
    # Create off-screen browser
    width, height = 1280, 720
    render_handler = RenderHandler(width, height)
    window_info = cef.WindowInfo()
    window_info.SetAsOffscreen(0)
    browser = cef.CreateBrowserSync(window_info=window_info,
                                    url="about:blank",
                                    settings={"windowless_frame_rate": 30},
                                    render_handler=render_handler)
    browser.SetClientHandler(render_handler)
    # Start servers
    threading.Thread(target=start_frame_server, args=(render_handler,), daemon=True).start()
    threading.Thread(target=start_cmd_server, args=(browser,), daemon=True).start()
    # Message loop
    while True:
        cef.MessageLoopWork()
        if not browser.IsValid():
            break
    cef.Shutdown()

if __name__ == "__main__":
    main()
