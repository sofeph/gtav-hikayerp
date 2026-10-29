import json
from typing import AsyncIterator

import httpx

from .base import ProviderError


class KeylessProvider:
    """OpenAI uyumlu /chat/completions endpoint'i (anahtarsız ücretsiz kaynaklar)."""

    local = False

    def __init__(self, name: str, base_url: str, model: str, idle_timeout_s: float,
                 max_tokens: int | None = None, api_key: str | None = None,
                 extra: dict | None = None) -> None:
        self.name = name
        self.extra = extra or {}
        self.base_url = base_url.rstrip("/")
        self.model = model
        self.max_tokens = max_tokens
        self.api_key = api_key
        self._timeout = httpx.Timeout(connect=5, read=idle_timeout_s, write=5, pool=5)

    async def stream(self, messages: list[dict]) -> AsyncIterator[str]:
        payload: dict = {"model": self.model, "messages": messages, "stream": True, "temperature": 0.7}
        if self.max_tokens:
            payload["max_tokens"] = self.max_tokens
        payload.update(self.extra)
        headers = {"Authorization": f"Bearer {self.api_key}"} if self.api_key else {}
        async with httpx.AsyncClient(timeout=self._timeout) as client:
            async with client.stream("POST", f"{self.base_url}/chat/completions",
                                     json=payload, headers=headers) as r:
                if r.status_code != 200:
                    body = (await r.aread()).decode("utf-8", "replace")[:200]
                    raise ProviderError(f"{self.name}: HTTP {r.status_code} {body}")
                async for line in r.aiter_lines():
                    if not line.startswith("data:"):
                        continue
                    data = line[5:].strip()
                    if data == "[DONE]":
                        return
                    try:
                        delta = json.loads(data)["choices"][0].get("delta", {})
                    except (json.JSONDecodeError, KeyError, IndexError):
                        continue
                    if text := delta.get("content"):
                        yield text
