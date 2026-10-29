import numpy as np
import sounddevice as sd

SAMPLE_RATE = 16000  # Whisper'ın beklediği örnekleme hızı


class PushToTalkRecorder:
    """start() ile kayda başlar, stop() kaydı float32 mono dizi olarak döndürür."""

    def __init__(self, device: int | str | None = None) -> None:
        self.device = device
        self._chunks: list[np.ndarray] = []
        self._stream: sd.InputStream | None = None

    def start(self) -> None:
        self._chunks = []
        self._stream = sd.InputStream(samplerate=SAMPLE_RATE, channels=1, dtype="float32",
                                      device=self.device, callback=self._on_audio)
        self._stream.start()

    def _on_audio(self, indata, frames, time_info, status) -> None:
        self._chunks.append(indata[:, 0].copy())

    def stop(self) -> np.ndarray:
        if self._stream:
            self._stream.stop()
            self._stream.close()
            self._stream = None
        return np.concatenate(self._chunks) if self._chunks else np.zeros(0, dtype="float32")
