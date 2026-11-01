with open('server.py', 'r') as f:
    content = f.read()

# Find and fix the problematic section
old = '''    async def turn(self, ws, session: Session, audio) -> None:
        """Sesli tur: mikrofon kaydı -> STT -> ortak cevap hattı."""
        async with self.turn_lock:
            loop = asyncio.get_running_loop()
                # Wait for STT to load if still loading
                while self.stt_loading:
                    await asyncio.sleep(0.1)
                if self.stt is None:
                    log.warning("STT henuz yuklenmedi, ses turu atlaniyor")
                    return
                t0 = time.perf_counter()
                text = await loop.run_in_executor(None, self.stt.transcribe, audio)
                log.info("STT %.2fs: %r", time.perf_counter() - t0, text)
                if text:
                    await self._reply(ws, session, text, t0)'''

new = '''    async def turn(self, ws, session: Session, audio) -> None:
        """Sesli tur: mikrofon kaydı -> STT -> ortak cevap hattı."""
        async with self.turn_lock:
            loop = asyncio.get_running_loop()
            # Wait for STT to load if still loading
            while self.stt_loading:
                await asyncio.sleep(0.1)
            if self.stt is None:
                log.warning("STT henuz yuklenmedi, ses turu atlaniyor")
                return
            t0 = time.perf_counter()
            text = await loop.run_in_executor(None, self.stt.transcribe, audio)
            log.info("STT %.2fs: %r", time.perf_counter() - t0, text)
            if text:
                await self._reply(ws, session, text, t0)'''

if old in content:
    content = content.replace(old, new)
    with open('server.py', 'w') as f:
        f.write(content)
    print('Fixed!')
else:
    print('Pattern not found')