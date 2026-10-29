import numpy as np
from faster_whisper import WhisperModel


class WhisperSTT:
    """Model açılışta bir kez yüklenir ve ısıtılır (gecikme bütçesi, CLAUDE.md §12)."""

    def __init__(self, model: str = "small", device: str = "auto", compute_type: str = "auto",
                 language: str = "tr") -> None:
        self.language = language
        self._model = WhisperModel(model, device=device, compute_type=compute_type)
        self.transcribe(np.zeros(16000, dtype="float32"))  # warm-up

    def transcribe(self, samples: np.ndarray) -> str:
        if samples.size < 1600:  # 0.1 sn'den kısa: boş say
            return ""
        segments, _ = self._model.transcribe(samples, language=self.language, beam_size=1,
                                             vad_filter=True, condition_on_previous_text=False)
        return " ".join(s.text.strip() for s in segments).strip()
