"""Owns the isolated native CEF browser host for the lifetime of the companion."""
from __future__ import annotations

import ctypes
import logging
import os
import pathlib
import subprocess
import threading

log = logging.getLogger("server")

_PIPE = r"\\.\pipe\HikayeTurkRP.Browser.v1"
_INVALID_HANDLE = ctypes.c_void_p(-1).value


class BrowserHostManager:
    def __init__(self) -> None:
        self.process: subprocess.Popen | None = None
        self.runtime_dir: pathlib.Path | None = None
        self._stop_event = threading.Event()
        self._monitor: threading.Thread | None = None

    def start(self) -> bool:
        if os.name != "nt":
            log.info("CEF browser host yalnızca Windows'ta çalışır.")
            return False

        configured = os.environ.get("HIKAYE_CEF_DIR")
        if configured:
            runtime = pathlib.Path(configured).resolve()
        else:
            runtime = (pathlib.Path(__file__).resolve().parent.parent /
                       "mod" / "build" / "cef").resolve()
        host = runtime / "HikayeTurkRPBrowserHost.exe"
        if not host.is_file() or not (runtime / "libcef.dll").is_file():
            log.warning("CEF browser host/runtime bulunamadı: %s", runtime)
            return False

        self.runtime_dir = runtime
        self.process = self._launch()
        if self.process is None:
            return False

        if self.process.poll() is not None:
            if self.process.returncode == 2:
                log.info("CEF browser host zaten çalışıyor.")
                self.process = None
                return True
            log.error("CEF browser host beklenmedik biçimde kapandı: exit=%s",
                      self.process.returncode)
            self.process = None
            return False
        log.info("CEF browser host başlatıldı (pid=%s).", self.process.pid)
        self._monitor = threading.Thread(target=self._monitor_process, daemon=True)
        self._monitor.start()
        return True

    def _launch(self) -> subprocess.Popen | None:
        assert self.runtime_dir is not None
        executable = self.runtime_dir / "HikayeTurkRPBrowserHost.exe"
        env = os.environ.copy()
        env["HIKAYE_CEF_DIR"] = str(self.runtime_dir)
        try:
            return subprocess.Popen(
                [str(executable)],
                cwd=str(self.runtime_dir),
                env=env,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                creationflags=subprocess.CREATE_NO_WINDOW,
            )
        except OSError:
            log.exception("CEF browser host yeniden başlatılamadı")
            return None

    def _monitor_process(self) -> None:
        while not self._stop_event.wait(2):
            child = self.process
            if child is None or child.poll() is None:
                continue
            code = child.returncode
            self.process = None
            if code == 2:
                log.info("Başka bir CEF browser host örneği çalışıyor.")
                return
            log.warning("CEF browser host kapandı (exit=%s); yeniden başlatılıyor.", code)
            self._stop_event.wait(1)
            if self._stop_event.is_set():
                return
            self.process = self._launch()

    @staticmethod
    def _send_stop() -> None:
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CreateFileW.restype = ctypes.c_void_p
        kernel32.CreateFileW.argtypes = (
            ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
            ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32,
            ctypes.c_void_p,
        )
        kernel32.WriteFile.argtypes = (
            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p,
        )
        kernel32.CloseHandle.argtypes = (ctypes.c_void_p,)
        handle = kernel32.CreateFileW(
            _PIPE, 0x40000000, 0, None, 3, 0, None,
        )
        if handle == _INVALID_HANDLE:
            return
        payload = ctypes.create_string_buffer(b"STOP\n")
        written = ctypes.c_ulong()
        kernel32.WriteFile(handle, payload, 5,
                           ctypes.byref(written), None)
        kernel32.CloseHandle(handle)

    def stop(self) -> None:
        self._stop_event.set()
        if self._monitor is not None:
            self._monitor.join(timeout=3)
            self._monitor = None
        child = self.process
        self.process = None
        if child is None or child.poll() is not None:
            return
        self._send_stop()
        try:
            child.wait(timeout=3)
        except subprocess.TimeoutExpired:
            log.warning("CEF browser host yanıt vermedi; başlatılan süreç sonlandırılıyor.")
            child.terminate()
            try:
                child.wait(timeout=2)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=2)
