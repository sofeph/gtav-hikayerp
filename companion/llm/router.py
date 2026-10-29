import asyncio
import logging
import time
from collections import deque
from typing import AsyncIterator

import httpx

from .base import LLMProvider, ProviderError, ThinkStripper
from .keyless import KeylessProvider
from .ollama import OllamaProvider

log = logging.getLogger("llm.router")


class AllProvidersFailed(Exception):
    pass


class RateLimiter:
    """Kayan pencere. Limit doluysa beklemez, çağıran sıradaki provider'a geçer (gecikme düşmandır)."""

    def __init__(self, max_calls: int, window_s: float) -> None:
        self.max_calls = max_calls
        self.window_s = window_s
        self._calls: deque[float] = deque()

    def try_acquire(self) -> bool:
        now = time.monotonic()
        while self._calls and now - self._calls[0] > self.window_s:
            self._calls.popleft()
        if len(self._calls) >= self.max_calls:
            return False
        self._calls.append(now)
        return True


class _Entry:
    def __init__(self, provider: LLMProvider, limiter: RateLimiter | None) -> None:
        self.provider = provider
        self.limiter = limiter


class LLMRouter:
    def __init__(self, cfg: dict) -> None:
        self.first_token_timeout_s = cfg.get("first_token_timeout_s", 4)
        self.strip_think = cfg.get("strip_think", True)
        self.min_attempts = cfg.get("min_attempts", 5)  # pes etmeden önce denenecek en az sağlayıcı sayısı
        idle = cfg.get("idle_timeout_s", 15)
        max_tokens = cfg.get("max_tokens")
        self.entries: list[_Entry] = []
        for c in cfg["chain"]:
            if c["type"] == "keyless":
                p: LLMProvider = KeylessProvider(c["name"], c["base_url"], c["model"], idle,
                                    c.get("max_tokens", max_tokens), extra=c.get("extra"))
            elif c["type"] == "ollama":
                p = OllamaProvider(c["name"], c["base_url"], c["model"], idle, max_tokens)
            else:
                raise ValueError(f"bilinmeyen provider türü: {c['type']}")
            lim = c.get("limit")
            self.entries.append(_Entry(p, RateLimiter(lim["max"], lim["window_s"]) if lim else None))
        self.last_provider: str | None = None

    async def stream(self, messages: list[dict], *, cloud_messages: list[dict] | None = None) -> AsyncIterator[str]:
        """`messages` local provider'lara, `cloud_messages` (anonimleştirilmiş) uzak provider'lara gider."""
        cloud_messages = cloud_messages if cloud_messages is not None else messages
        errors: list[str] = []
        attempts = 0
        # 1. tur: hız sınırlarına uyarak sırayla dene. En az `min_attempts` FARKLI sağlayıcı denenmeden pes etme (ghosting olmasın):
        # sınır yüzünden atlananlar, deneme sayısı dolana kadar 2. turda sınır yok sayılarak denenir.
        skipped: list[_Entry] = []
        for e in list(self.entries) + [None]:
            if e is None:  # 2. tur
                if attempts >= self.min_attempts:
                    break
                for e2 in skipped:
                    if attempts >= self.min_attempts:
                        break
                    r_ = await self._try(e2, messages, cloud_messages, errors)
                    attempts += 1
                    if r_ is not None:
                        async for out in r_:
                            yield out
                        return
                break
            p = e.provider
            if e.limiter and not e.limiter.try_acquire():
                errors.append(f"{p.name}: limit dolu")
                skipped.append(e)
                continue
            r_ = await self._try(e, messages, cloud_messages, errors)
            attempts += 1
            if r_ is not None:
                async for out in r_:
                    yield out
                return
        raise AllProvidersFailed("; ".join(errors))

    async def _try(self, e: "_Entry", messages, cloud_messages, errors):
        """Sağlayıcıyı dene; ilk parça gelirse kalan akışı veren bir async generator döndür, olmadı None."""
        p = e.provider
        if True:
            gen = p.stream(messages if p.local else cloud_messages)
            try:
                first = await asyncio.wait_for(gen.__anext__(), self.first_token_timeout_s)
            except (ProviderError, httpx.HTTPError, asyncio.TimeoutError, StopAsyncIteration) as ex:
                errors.append(f"{p.name}: {type(ex).__name__} {ex}")
                log.warning("provider elendi: %s", errors[-1])
                await gen.aclose()
                return None
            self.last_provider = p.name
            log.info("provider: %s", p.name)
            stripper = ThinkStripper() if self.strip_think else None

            async def rest():
                for chunk in (first,):
                    out = stripper.feed(chunk) if stripper else chunk
                    if out:
                        yield out
                async for chunk in gen:
                    out = stripper.feed(chunk) if stripper else chunk
                    if out:
                        yield out
                if stripper and (tail := stripper.flush()):
                    yield tail

            return rest()
