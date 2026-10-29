import os

import numpy as np
import torch

# Coqui CPML (ticari olmayan) lisansı: mod ücretsiz dağıtılıyor, satılmıyor. Etkileşimli [y/n] sorusu
# sunucuda stdin olmadığı için yükleme sırasında takılıyordu.
os.environ.setdefault("COQUI_TOS_AGREED", "1")

from TTS.api import TTS  # noqa: E402


class XTTS:
    """Coqui XTTS-v2: yerel, çok sesli, Türkçe destekli (ElevenLabs benzeri).

    Ses tanımı (`voice`):
      {"speaker": "Claribel Dervla"}                  -> yerleşik stüdyo sesi
      {"reference_wav": "voices/ramazan.wav"}         -> ses klonlama (6-15 sn temiz klip)
    NOT: XTTS-v2 lisansı (CPML) ticari kullanıma izin vermez; kişisel mod için uygundur.
    """

    sample_rate = 24000

    def __init__(self, device: str | None = None, language: str = "tr") -> None:
        self.language = language
        device = device or ("cuda" if torch.cuda.is_available() else "cpu")
        self._tts = TTS("tts_models/multilingual/multi-dataset/xtts_v2").to(device)

    def synthesize(self, text: str, voice: dict) -> np.ndarray:
        kwargs = {"text": text, "language": self.language}
        if ref := voice.get("reference_wav"):
            kwargs["speaker_wav"] = ref
        else:
            kwargs["speaker"] = voice.get("speaker", "Claribel Dervla")
        wav = self._tts.tts(**kwargs)
        return np.asarray(wav, dtype="float32")
