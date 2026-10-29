from typing import AsyncIterator, Protocol


class ProviderError(Exception):
    """Provider isteği başarısız (HTTP hatası, bozuk akış vb.)."""


class LLMProvider(Protocol):
    name: str
    local: bool

    def stream(self, messages: list[dict]) -> AsyncIterator[str]: ...


class ThinkStripper:
    """Akıştaki <think>...</think> bloklarını parça sınırlarından bağımsız ayıklar."""

    OPEN, CLOSE = "<think>", "</think>"

    def __init__(self) -> None:
        self._buf = ""
        self._in = False

    def feed(self, chunk: str) -> str:
        self._buf += chunk
        out = ""
        while True:
            if self._in:
                i = self._buf.find(self.CLOSE)
                if i < 0:
                    self._buf = self._buf[-(len(self.CLOSE) - 1):]
                    return out
                self._buf = self._buf[i + len(self.CLOSE):]
                self._in = False
            else:
                i = self._buf.find(self.OPEN)
                if i >= 0:
                    out += self._buf[:i]
                    self._buf = self._buf[i + len(self.OPEN):]
                    self._in = True
                    continue
                keep = self._partial_open_suffix()
                cut = len(self._buf) - keep
                out += self._buf[:cut]
                self._buf = self._buf[cut:]
                return out

    def flush(self) -> str:
        out = "" if self._in else self._buf
        self._buf = ""
        return out

    def _partial_open_suffix(self) -> int:
        for n in range(min(len(self.OPEN) - 1, len(self._buf)), 0, -1):
            if self.OPEN.startswith(self._buf[-n:]):
                return n
        return 0
