import json
from typing import AsyncIterator

import httpx

from .base import ProviderError


class OllamaProvider:
    """Local yedek. Tamamen offline çalışır."""

    local = True

    def __init__(self, name: str, base_url: str, model: str, idle_timeout_s: float,
                 max_tokens: int | None = None) -> None:
        self.name = name
        self.base_url = base_url.rstrip("/")
        self.model = model
        self.max_tokens = max_tokens
        self._timeout = httpx.Timeout(connect=3, read=idle_timeout_s, write=5, pool=5)

    async def stream(self, messages: list[dict]) -> AsyncIterator[str]:
        payload: dict = {"model": self.model, "messages": messages, "stream": True, "format": "json"}
        if self.max_tokens:
            payload["options"] = {"num_predict": self.max_tokens}
        async with httpx.AsyncClient(timeout=self._timeout) as client:
            async with client.stream("POST", f"{self.base_url}/api/chat", json=payload) as r:
                if r.status_code != 200:
                    body = (await r.aread()).decode("utf-8", "replace")[:200]
                    raise ProviderError(f"{self.name}: HTTP {r.status_code} {body}")
                async for line in r.aiter_lines():
                    if not line:
                        continue
                    try:
                        obj = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if text := obj.get("message", {}).get("content"):
                        yield text
                    if obj.get("done"):
                        return
