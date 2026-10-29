from typing import Protocol

import numpy as np


class TTSEngine(Protocol):
    sample_rate: int

    def synthesize(self, text: str, voice: dict) -> np.ndarray:
        """float32 mono ses döndürür. `voice` NPC'nin config'teki ses tanımıdır."""
        ...
