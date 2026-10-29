import pathlib

import numpy as np
from piper import PiperVoice


class PiperTTS:
    """Hızlı CPU yedeği. Türkçe için tek ses var (dfki); NPC'ler arası ayrım için XTTS kullan."""

    def __init__(self, model_dir: str | pathlib.Path, default_voice: str = "tr_TR-dfki-medium") -> None:
        self.model_dir = pathlib.Path(model_dir)
        self.default_voice = default_voice
        self._voices: dict[str, PiperVoice] = {}
        self.sample_rate = self._load(default_voice).config.sample_rate

    def _load(self, name: str) -> PiperVoice:
        if name not in self._voices:
            self._voices[name] = PiperVoice.load(self.model_dir / f"{name}.onnx")
        return self._voices[name]

    def synthesize(self, text: str, voice: dict) -> np.ndarray:
        v = self._load(voice.get("model", self.default_voice))
        pcm = b"".join(chunk.audio_int16_bytes for chunk in v.synthesize(text))
        return np.frombuffer(pcm, dtype=np.int16).astype("float32") / 32768.0
