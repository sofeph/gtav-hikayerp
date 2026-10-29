import numpy as np
import sounddevice as sd


def play(samples: np.ndarray, sample_rate: int, device: int | str | None = None, block: bool = True) -> None:
    sd.play(samples, sample_rate, device=device)
    if block:
        sd.wait()


def stop() -> None:
    sd.stop()
