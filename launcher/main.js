'use strict';
const { app, BrowserWindow, ipcMain, dialog, shell, safeStorage, clipboard } = require('electron');
const path = require('path');
const fs = require('fs');
const os = require('os');
const core = require('./core');
const inst = require('./instance');

// Dağıtım kökü: paketlenmişte <kök>/launcher/HikayeTurkRP-Launcher.exe -> kök bir üst klasör; geliştirmede proje kökü.
// Kurulum (NSIS): mod/, gamefiles/, companion/ exe ile AYNI klasörde; zip düzeni: exe launcher/ altında, kök bir üstte.
const EXE_DIR = path.dirname(process.execPath);
const BUNDLE = app.isPackaged ? (fs.existsSync(path.join(EXE_DIR, 'mod')) ? EXE_DIR : path.resolve(EXE_DIR, '..')) : path.resolve(__dirname, '..');
// KENDİ DİZİNİNDE ÇALIŞIR: kurulum klasörü yazılabiliyorsa (varsayılan kurulum %LOCALAPPDATA%\Programs\Hikaye RP, ya da seçtiğin klasör)
// ayar, vendor, TR yama, güncellemeler ve sunucu doğrudan orada durur. Program Files gibi yazılamaz bir yerdeyse
// paket kullanıcının yazılabilir klasörüne (%LOCALAPPDATA%\HikayeTurkRP\Pkg) alınıp oradan çalışır.
const WORK = path.join(process.env.LOCALAPPDATA || path.join(os.homedir(), 'AppData', 'Local'), 'HikayeTurkRP', 'Pkg');
const OWN_DIR = !app.isPackaged || core.isWritable(BUNDLE);
if (!OWN_DIR) {
  try { core.syncBundle(BUNDLE, WORK, () => {}); } catch (e) { console.error('paket eşitlenemedi: ' + e.message); }
}
const ROOT = OWN_DIR ? BUNDLE : WORK;
const LAY = core.layout(ROOT);
const SETTINGS_FILE = path.join(ROOT, 'launcher-settings.json');
const DEFAULTS = { gameDir: '', instanceDir: '', python: '', port: 8765, githubRepo: 'sofeph/gtav-hikayerp', tokenEnc: '', autoCheck: true, launchMode: 'steam', trPatch: true, autoMenu: true, autoMenuDelay: 10, autoMenuInject: false, autoMenuPy: true, autoMenuButton: 'BACK', autoMenuKey: 'BACKSPACE', autoMenuKey: '{BACKSPACE}', gameArgs: '-scOfflineOnly -nobattleye' };

let win = null;
let server = null;
let busy = false;

function settings() { return Object.assign({}, DEFAULTS, core.readJson(SETTINGS_FILE, {})); }
function saveSettings(s) { core.writeJson(SETTINGS_FILE, s); }

// GitHub erişim anahtarı (özel depo için, isteğe bağlı): işletim sistemi şifrelemesiyle saklanır, düz metin yazılmaz.
function getToken() {
  const s = settings();
  if (!s.tokenEnc) return '';
  try { return safeStorage.decryptString(Buffer.from(s.tokenEnc, 'base64')); } catch (e) { return ''; }
}
function setToken(t) {
  const s = settings();
  if (!t) s.tokenEnc = '';
  else if (safeStorage.isEncryptionAvailable()) s.tokenEnc = safeStorage.encryptString(t).toString('base64');
  else throw new Error('Bu bilgisayarda anahtar şifrelenemiyor; anahtar saklanmadı.');
  saveSettings(s);
}

function send(ch, payload) { if (win && !win.isDestroyed()) win.webContents.send(ch, payload); }
function log(line) { send('log', '[' + new Date().toLocaleTimeString('tr-TR') + '] ' + line); }
function instDir(s) { return (s || settings()).instanceDir || inst.defaultInstanceDir(); }

function pushState() {
  const s = settings();
  const dir = core.findGameDir(s);
  const st = core.state(dir, LAY);
  const idir = instDir(s);
  st.inst = inst.state(dir, idir, LAY);
  st.patch = core.patchState(idir);               // Türkçe yama oyun KOPYASINDAKİ mods klasörüdür
  st.patch.source = !!core.trPatchSource(LAY);
  st.installed = st.inst.exists;
  st.upToDate = st.inst.upToDate;
  st.serverRunning = !!(server && server.alive);
  st.gameRunning = core.gameRunning();
  st.python = core.checkPython(s.python);
  send('state', st);
  return st;
}

function createWindow() {
  win = new BrowserWindow({
    width: 980, height: 700, minWidth: 820, minHeight: 600, backgroundColor: '#12151c', title: 'Hikaye RP Başlatıcı', icon: path.join(__dirname, 'renderer', 'logo.png'),
    webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, nodeIntegration: false, sandbox: false },
  });
  win.setMenuBarVisibility(false);
  win.loadFile(path.join(__dirname, 'renderer', 'index.html'));
  if (process.env.HIKAYE_SMOKE) {  // arayüz duman testi: konsol hataları + ekran görüntüsü (HIKAYE_SMOKE=<png yolu>), sonra çık
    const errs = [];
    win.webContents.on('console-message', (_e, level, msg) => { if (level >= 2) errs.push(msg); });
    win.webContents.once('did-finish-load', async () => {
      await new Promise((r) => setTimeout(r, 1500));
      for (const tab of ['settings', 'play']) {
        await win.webContents.executeJavaScript("document.querySelector('[data-tab=" + tab + "]').click()");
        await new Promise((r) => setTimeout(r, 1200));
        const img = await win.webContents.capturePage();
        fs.writeFileSync(process.env.HIKAYE_SMOKE.replace(/\.png$/, '_' + tab + '.png'), img.toPNG());
      }
      fs.writeFileSync(process.env.HIKAYE_SMOKE.replace(/\.png$/, '.txt'), errs.join('\n') || 'hata yok');
      app.exit(0);
    });
  }
}

async function guarded(fn) {
  if (busy) return { ok: false, error: 'Başka bir işlem sürüyor.' };
  busy = true;
  try { return await fn(); } catch (e) { log('HATA: ' + e.message); return { ok: false, error: e.message }; } finally { busy = false; pushState(); }
}

ipcMain.handle('init', async () => {
  const s = settings();
  return { settings: Object.assign({}, s, { tokenEnc: undefined, hasToken: !!s.tokenEnc }), version: core.localVersion(LAY), state: pushState(), root: ROOT };
});

ipcMain.handle('save-settings', async (_e, patch) => {
  const s = settings();
  for (const k of ['gameDir', 'instanceDir', 'python', 'launchMode', 'gameArgs']) if (typeof patch[k] === 'string') s[k] = patch[k].trim();
  if (typeof patch.autoCheck === 'boolean') s.autoCheck = patch.autoCheck;
  if (typeof patch.trPatch === 'boolean') s.trPatch = patch.trPatch;
  if (typeof patch.autoMenu === 'boolean') s.autoMenu = patch.autoMenu;
  if (typeof patch.instanceDir === 'string' && patch.instanceDir) {
    const orig = core.findGameDir(s);
    if (orig && path.resolve(patch.instanceDir).toLowerCase().startsWith(path.resolve(orig).toLowerCase())) return { ok: false, error: 'Oyun kopyası orijinal GTA klasörünün içinde olamaz.' };
  }
  if (patch.githubRepo && !core.validRepo(s.githubRepo)) return { ok: false, error: 'Depo adı "kullanici/depo" biçiminde olmalı.' };
  saveSettings(s);
  if (typeof patch.token === 'string') { try { setToken(patch.token.trim()); } catch (e) { return { ok: false, error: e.message }; } }
  pushState();
  return { ok: true };
});

ipcMain.handle('pick-game-dir', async () => {
  const r = await dialog.showOpenDialog(win, { title: 'GTA V klasörünü seç (GTA5.exe olan)', properties: ['openDirectory'] });
  if (r.canceled || !r.filePaths[0]) return { ok: false };
  if (!core.isGameDir(r.filePaths[0])) return { ok: false, error: 'Bu klasörde GTA5.exe yok.' };
  const s = settings(); s.gameDir = r.filePaths[0]; saveSettings(s); pushState();
  return { ok: true, dir: r.filePaths[0] };
});

ipcMain.handle('install', () => guarded(async () => {
  const s = settings();
  const orig = core.findGameDir(s);
  if (!orig) return { ok: false, error: 'Orijinal GTA V klasörü bulunamadı (Ayarlar).' };
  if (core.gameRunning()) return { ok: false, error: 'Önce oyunu kapat.' };
  const r = inst.sync(orig, instDir(s), LAY, log, { args: settings().gameArgs });   // orijinale YAZILMAZ
  r.warnings.forEach((w) => log('UYARI: ' + w));
  return { ok: true, warnings: r.warnings };
}));

ipcMain.handle('uninstall', (_e, purge) => guarded(async () => {
  if (core.gameRunning()) return { ok: false, error: 'Önce oyunu kapat.' };
  inst.remove(instDir(settings()), !purge, log);
  log('Oyun kopyası silindi (orijinal GTA V\'ye dokunulmadı).');
  return { ok: true };
}));

// Otomatik hazırlık: yapılabilen her şeyi sırayla yapar, kalanı listeler
ipcMain.handle('auto-setup', () => guarded(async () => {
  const s = settings();
  const todo = [];
  const step = (t) => { log('▶ ' + t); send('step', t); };
  const orig = core.findGameDir(s);
  if (!orig) return { ok: false, error: 'GTA V klasörü bulunamadı: Ayarlar sekmesinden seç.' };
  if (core.gameRunning()) return { ok: false, error: 'Önce oyunu kapat.' };

  // 1) Python (taşınabilir: sisteme kurulum gerekmez)
  step('Python denetleniyor');
  let py = s.python || 'python';
  if (!core.checkPython(py)) {
    step('Taşınabilir Python kuruluyor (ilk seferde ~15 MB indirilir)…');
    try {
      py = await core.ensurePortablePython((l) => log('  ' + l));
      const ss = settings(); ss.python = py; saveSettings(ss);
    } catch (e) { log('  ' + e.message); }
    if (!core.checkPython(py)) { todo.push('Python kurulamadı: internete bağlı olduğundan emin ol ve tekrar dene.'); }
  }

  // 2) Sunucu paketleri
  if (core.checkPython(py)) {
    step('Sunucu paketleri denetleniyor');
    if (!core.pythonPackagesOk(py)) {
      step('Sunucu paketleri kuruluyor (pip; birkaç dakika sürebilir)…');
      try { await core.pipInstall(py, LAY.companionDir, (l) => send('log', '  pip: ' + l)); log('  paketler kuruldu'); }
      catch (e) { log('  ' + e.message); todo.push('Python paketleri kurulamadı: companion\\requirements-full.txt dosyasını elle kur.'); }
    } else log('  paketler zaten kurulu');
  }

  // 3) ScriptHookV / OpenIV (İndirilenler'den -> vendor; orijinal GTA'ya DEĞİL)
  step('ScriptHookV aranıyor (İndirilenler)');
  const v = inst.state(orig, instDir(s), LAY).vendor;
  if (!v['ScriptHookV.dll'] || !v['dinput8.dll'] || !v['OpenIV.asi']) {
    const got = core.importVendorFromDownloads(inst.vendorDir(LAY), log);
    if (got.length) log('  eklendi: ' + got.join(', '));
  }
  const v2 = inst.state(orig, instDir(s), LAY).vendor;
  if (!v2['ScriptHookV.dll'] || !v2['dinput8.dll']) todo.push('ScriptHookV\'yi dev-c.com/gtav/scripthookv adresinden indir (zip İndirilenler\'de dursun) ya da "ScriptHookV / OpenIV ekle…" ile seç.');

  // 4) Oyun kopyası
  step('Oyun kopyası hazırlanıyor (orijinal GTA V\'ye dokunulmaz)');
  try { inst.sync(orig, instDir(s), LAY, log, { args: settings().gameArgs }).warnings.forEach((w) => log('  UYARI: ' + w)); }
  catch (e) { log('  ' + e.message); todo.push(e.message); }

  // 5) Türkçe yama (ayar açıksa)
  if (s.trPatch !== false && core.trPatchSource(LAY) && !core.patchState(instDir(s)).present) {
    step('Türkçe yama kuruluyor…');
    try { (await core.installTrPatch(instDir(s), orig, LAY, log)).forEach((w) => log('  UYARI: ' + w)); } catch (e) { log('  ' + e.message); }
  }

  const left = todo.filter(Boolean);
  log(left.length ? 'Otomatik hazırlık bitti; elle yapılacaklar: ' + left.length : 'Otomatik hazırlık tamam: oyunu başlatabilirsin.');
  return { ok: true, todo: left };
}));

ipcMain.handle('install-addon', async () => {
  const s = settings();
  const r = await dialog.showOpenDialog(win, { title: 'Eklenti paketi (.zip) seç', properties: ['openFile'], filters: [{ name: 'Eklenti paketi', extensions: ['zip'] }] });
  if (r.canceled || !r.filePaths.length) return { ok: false, canceled: true };
  try {
    const res = core.installAddonZip(r.filePaths[0], LAY, instDir(s), log);
    (res.warnings || []).forEach((w) => log('Eklenti uyarısı: ' + w));
    pushState();
    return Object.assign({ ok: true, gameRunning: core.gameRunning() }, res);
  } catch (e) { return { ok: false, error: e.message }; }
});
ipcMain.handle('list-addons', () => ({ ok: true, addons: core.listAddons(LAY) }));
ipcMain.handle('remove-addon', (_e, id) => {
  try { core.removeAddon(id, LAY, instDir(settings()), log); return { ok: true, addons: core.listAddons(LAY) }; } catch (e) { return { ok: false, error: e.message }; }
});

ipcMain.handle('import-vendor', async () => {
  // 1) Önce OTOMATİK: İndirilenler/Masaüstü/OpenIV klasörleri/orijinal klasör taranır (yalnız okunur)
  const auto = core.importVendorFromDownloads(inst.vendorDir(LAY), log, core.findGameDir(settings()));
  const have = inst.state(core.findGameDir(settings()), instDir(settings()), LAY).vendor;
  if (auto.length) {
    pushState();
    const missing = ['ScriptHookV.dll', 'dinput8.dll', 'OpenIV.asi'].filter((f) => !have[f]);
    return { ok: true, added: auto, auto: true, missing };
  }
  // 2) Bulunamadı: elle seçtir
  const manual = await manualVendor();
  return manual;
});

async function manualVendor() {
  return importVendorDialog();
}

async function importVendorDialog() {
  const r = await dialog.showOpenDialog(win, { title: 'ScriptHookV.dll, dinput8.dll, OpenIV.asi dosyalarını seç', properties: ['openFile', 'multiSelections'], filters: [{ name: 'Dosyalar', extensions: ['dll', 'asi'] }] });
  if (r.canceled) return { ok: false };
  const added = inst.importVendor(LAY, r.filePaths, log);
  pushState();
  return { ok: added.length > 0, added };
}

// Başlat: gerekirse kur -> Python sunucusu -> oyun -> oyun bitince sunucuyu kapat (FiveM mantığı)
// Başlat'tan önce her şeyi kendiliğinden hazırlar: ScriptHookV/OpenIV (vendor), Python, sunucu paketleri
async function ensureEnv(s, orig) {
  const step = (x) => { log('▶ ' + x); send('step', x); };
  const v = inst.state(orig, instDir(s), LAY).vendor;
  if (!v['ScriptHookV.dll'] || !v['dinput8.dll'] || !v['OpenIV.asi']) {
    const got = core.importVendorFromDownloads(inst.vendorDir(LAY), log, orig);
    if (got.length) log('  eklendi: ' + got.join(', '));
  }
  let py = s.python || 'python';
  if (!core.checkPython(py)) {
    step('Taşınabilir Python kuruluyor…');
    try {
      py = await core.ensurePortablePython((l) => log('  ' + l));
      const ss = settings(); ss.python = py; saveSettings(ss);
    } catch (e) { log('  ' + e.message); }
  }
  if (core.checkPython(py) && !core.pythonPackagesOk(py)) {
    step('Sunucu paketleri kuruluyor (pip; ilk seferde birkaç dakika)…');
    try { await core.pipInstall(py, LAY.companionDir, (l) => send('log', '  pip: ' + l)); log('  paketler kuruldu'); } catch (e) { log('  ' + e.message); }
  }
}

ipcMain.handle('start', () => guarded(async () => {
  const s = settings();
  const dir = core.findGameDir(s);
  if (!dir) return { ok: false, error: 'Oyun klasörü bulunamadı (Ayarlar).' };
  await ensureEnv(s, dir);
  s.python = settings().python;
  const idir = instDir(s);
  const ist = inst.state(dir, idir, LAY);
  if (!ist.exists || !ist.upToDate) {
    if (core.gameRunning()) log('Oyun açık: kopya güncellenemedi, mevcut sürümle devam.');
    else { log('Oyun kopyası hazırlanıyor/yenileniyor (orijinal GTA V\'ye dokunulmaz)...'); inst.sync(dir, idir, LAY, log, { args: settings().gameArgs }).warnings.forEach((w) => log('UYARI: ' + w)); }
  }
  {   // Türkçe yama varsayılan AKTİF (Ayarlar'dan kapatılabilir): kopyada yoksa kurulur, kapalıysa açılır; ayar kapalıysa kapatılır
    const ps = core.patchState(idir);
    try {
      if (s.trPatch) {
        if (!ps.present && core.trPatchSource(LAY)) { (await core.installTrPatch(idir, dir, LAY, log)).forEach((w) => log('UYARI: ' + w)); }
        else if (ps.present && !ps.enabled) { core.togglePatch(idir, true); log('Türkçe yama açıldı.'); }
        else if (!ps.present) log('Türkçe yama paketi bulunamadı (tr-yama/mods): yamasız devam. Yama zip\'lerini paket klasörüne aç.');
      } else if (ps.present && ps.enabled) { core.togglePatch(idir, false); log('Türkçe yama ayarlardan kapalı: devre dışı bırakıldı.'); }
    } catch (e) { log('Türkçe yama: ' + e.message); }
  }
  if (!core.checkPython(s.python)) {
    log('Taşınabilir Python ortamı kuruluyor (ilk seferde)...');
    try {
      const newPy = await core.ensurePortablePython((l) => log('  ' + l));
      s.python = newPy;
      saveSettings(s);
    } catch (e) {
      return { ok: false, error: 'Yapay zekâ taşınabilir Python ortamı kurulamadı: ' + e.message };
    }
  }
  if (await core.portOpen(s.port)) {
    log('Sunucu zaten çalışıyor (port ' + s.port + '), yeniden başlatılmadı.');
    server = null;
  } else {
    log('Sunucu başlatılıyor...');
    process.env.HIKAYE_AUTOMENU = '0';  // Python automenu devre dışı; geçiş RAGE native (SHUTDOWN_LOADING_SCREEN + _SET_CONTROL_NORMAL)
    process.env.HIKAYE_AUTOBACK_KEY = String(s.autoMenuKey || 'BACKSPACE').replace(/[{}]/g, '').toUpperCase();
    server = core.startServer(LAY, s.python, (l) => send('log', '  sunucu: ' + l));
    const srv = server;
    const ready = await core.waitPort(s.port, 240000, () => srv.alive);
    if (!ready) { core.killTree(server); server = null; return { ok: false, error: 'Sunucu açılamadı (log\'a bak). Gerekli Python paketleri kurulu mu? (companion/requirements-full.txt)' }; }
    log('Sunucu hazır.');
  }
  pushState();
  if (!core.gameRunning()) {
    log('Oyun başlatılıyor (kopyadan)...');
    const { spawn } = require('child_process');
    const exe = 'PlayGTAV.exe';  // GTA5.exe doğrudan açılırsa ERR_NO_LAUNCHER verir: Steam başlatıcısı üzerinden
    if (!fs.existsSync(path.join(idir, exe))) return { ok: false, error: exe + ' kopyada yok.' };
    log("İPUCU: 'BattlEye Protected Launch Failed' penceresi çıkarsa RETRY'a bas: oyun BattlEye'siz tek oyunculu modda açılır (ScriptHookV için gerekli, normal). Pencere her başlatmada çıkabilir.");
    // GTA save swap: 100pct/ dosyası varsa orijinali yedekle ve 100% save yükle; yoksa atla.
    swapToHikayeSaves();
    // Steam açık olmalı; steam_appid.txt kopyada. Orijinal klasör hiç kullanılmaz.
    spawn(path.join(idir, exe), [], { cwd: idir, detached: true, stdio: 'ignore', env: Object.assign({}, process.env, { SteamAppId: String(core.STEAM_APP_ID), SteamGameId: String(core.STEAM_APP_ID), HIKAYE_LAUNCHER: '1', HIKAYE_CEF_DIR: path.join(idir, 'HikayeTurkRP', 'cef'), HIKAYE_AUTOBACK: s.autoMenuInject ? '1' : '0', HIKAYE_AUTOMENU: '0', HIKAYE_AUTOBACK_DELAY: String(s.autoMenuDelay || 25), HIKAYE_AUTOBACK_BTN: String(s.autoMenuButton || 'BACK').toUpperCase(), HIKAYE_AUTOBACK_KEY: String(s.autoMenuKey || 'BACKSPACE').replace(/[{}]/g, '').toUpperCase() }) }).unref();
  } else { log('Oyun zaten açık.'); }
  watchGame();
  return { ok: true };
}));

// ──────────────────────────────────────────────────────────────
//  GTA V Save Swap — 100% save yükle / orijinalini geri getir
// ──────────────────────────────────────────────────────────────
const GTA_PROFILES = path.join(app.getPath('documents'), 'Rockstar Games', 'GTA V', 'Profiles');
const SAVE_BACKUP  = path.join(process.env.LOCALAPPDATA || path.join(os.homedir(), 'AppData', 'Local'), 'HikayeTurkRP', 'save_backup');
// 100% save, oyun kopyasının HikayeTurkRP/data/saves/100pct/ altında; instDir() ile dinamik alınır.
function save100PctDir() { return path.join(instDir(), 'HikayeTurkRP', 'data', 'saves', '100pct'); }

function gtaProfileDir() {
  if (!fs.existsSync(GTA_PROFILES)) return null;
  const dirs = fs.readdirSync(GTA_PROFILES).filter(d => /^[0-9A-Fa-f]{8}$/i.test(d));
  return dirs.length > 0 ? path.join(GTA_PROFILES, dirs[0]) : null;
}

function backupGtaSaves() {
  const prof = gtaProfileDir();
  if (!prof) { log('save-swap: GTA profil klasörü bulunamadı, yedek alınmadı.'); return; }
  if (fs.existsSync(SAVE_BACKUP)) {
    log('save-swap: Önceki yedek mevcut, üzerine yazılıyor.');
    fs.rmSync(SAVE_BACKUP, { recursive: true, force: true });
  }
  fs.mkdirSync(SAVE_BACKUP, { recursive: true });
  let n = 0;
  for (const f of fs.readdirSync(prof)) {
    if (/^SGTA5/i.test(f)) { fs.copyFileSync(path.join(prof, f), path.join(SAVE_BACKUP, f)); ++n; }
  }
  log('save-swap: ' + n + ' save dosyası yedeklendi → ' + SAVE_BACKUP);
}

function install100PctSave() {
  const dir100 = save100PctDir();
  if (!fs.existsSync(dir100)) { log('save-swap: 100pct/ klasörü yok, atlandı: ' + dir100); return; }
  const files = fs.readdirSync(dir100).filter(f => /^SGTA5/i.test(f));
  if (files.length === 0) { log('save-swap: 100pct/ klasöründe SGTA5* dosyası yok, atlandı.'); return; }
  const prof = gtaProfileDir();
  if (!prof) return;
  for (const f of files) fs.copyFileSync(path.join(dir100, f), path.join(prof, f));
  log('save-swap: ' + files.length + ' 100% save yüklendi.');
}

function swapToHikayeSaves() {
  const dir100 = save100PctDir();
  if (!fs.existsSync(dir100)) return;  // 100pct/ yoksa hiç dokunma
  const files = fs.readdirSync(dir100).filter(f => /^SGTA5/i.test(f));
  if (files.length === 0) return;       // içinde save dosyası yoksa hiç dokunma
  backupGtaSaves();
  install100PctSave();
}

function restoreGtaSaves() {
  if (!fs.existsSync(SAVE_BACKUP)) { log('save-swap: Yedek yok, geri yükleme yapılmadı.'); return; }
  const prof = gtaProfileDir();
  if (!prof) { log('save-swap: GTA profil klasörü bulunamadı, geri yükleme yapılamadı.'); return; }
  // Mevcut SGTA5* dosyalarını sil
  for (const f of fs.readdirSync(prof)) {
    if (/^SGTA5/i.test(f)) { try { fs.unlinkSync(path.join(prof, f)); } catch (_) {} }
  }
  let n = 0;
  for (const f of fs.readdirSync(SAVE_BACKUP)) {
    fs.copyFileSync(path.join(SAVE_BACKUP, f), path.join(prof, f)); ++n;
  }
  fs.rmSync(SAVE_BACKUP, { recursive: true, force: true });
  log('save-swap: ' + n + ' orijinal save geri yüklendi.');
}

let watching = false;
async function watchGame() {
  if (watching) return;
  watching = true;
  log('Oyun bekleniyor...');
  const t0 = Date.now();
  while (!core.gameRunning() && Date.now() - t0 < 300000) await new Promise((r) => setTimeout(r, 2000));
  if (core.gameRunning()) {
    log('Oyun çalışıyor. Kapanınca sunucu da kapanacak.');
    while (core.gameRunning()) { pushState(); await new Promise((r) => setTimeout(r, 4000)); }
    log('Oyun kapandı.');
    restoreGtaSaves();
  } else { log('Oyun 5 dakikada açılmadı.'); restoreGtaSaves(); }
  if (server) { core.killTree(server); server = null; log('Sunucu durduruldu.'); }
  watching = false;
  pushState();
}

ipcMain.handle('stop', () => guarded(async () => {
  if (server) { core.killTree(server); server = null; log('Sunucu durduruldu.'); }
  return { ok: true };
}));

ipcMain.handle('check-update', async () => {
  try {
    const r = await core.checkUpdate(LAY, settings(), getToken());
    if (r.ok) log(r.available ? 'Yeni sürüm var: ' + r.latest + ' (şu an ' + r.current + ')' : 'Güncelsin (' + r.current + ').');
    else log('Güncelleme: ' + r.reason);
    return r;
  } catch (e) { log('Güncelleme denetimi başarısız: ' + e.message); return { ok: false, reason: e.message }; }
});

ipcMain.handle('apply-update', (_e, info) => guarded(async () => {
  if (!info || !info.asset) return { ok: false, error: 'Sürümde indirilecek .zip yok.' };
  if (core.gameRunning()) return { ok: false, error: 'Önce oyunu kapat.' };
  if (server) { core.killTree(server); server = null; }
  const zip = path.join(os.tmpdir(), 'HikayeTurkRP-update-' + info.latest + '.zip');
  log('İndiriliyor: ' + info.asset.name);
  let last = -10;
  await core.downloadAsset(info, getToken(), zip, (p) => { if (p - last >= 10) { last = p; send('progress', p); } });
  log('Uygulanıyor...');
  const v = core.applyUpdate(zip, ROOT, log);
  fs.rmSync(zip, { force: true });
  const dir = core.findGameDir(settings());
  if (dir) inst.sync(dir, instDir(settings()), core.layout(ROOT), log, { args: settings().gameArgs });  // kopya yenilenir; orijinale yazılmaz
  log('Güncellendi: ' + v + (info.latest && v !== info.latest ? ' (paket sürümü ' + v + ')' : ''));
  return { ok: true, version: v, restart: true };
}));

ipcMain.handle('install-tr-patch', () => guarded(async () => {
  const warn = await core.installTrPatch(instDir(settings()), core.findGameDir(settings()), LAY, log);
  warn.forEach((w) => log('UYARI: ' + w));
  return { ok: true, warnings: warn };
}));

ipcMain.handle('toggle-patch', (_e, enable) => guarded(async () => {
  const p = core.togglePatch(instDir(settings()), !!enable);
  log('Türkçe yama (mods klasörü) ' + (p.enabled ? 'AÇILDI' : 'KAPATILDI') + ' - oyunu yeniden başlat');
  return { ok: true };
}));

ipcMain.handle('open', (_e, what) => {
  const d = instDir(settings());
  const map = { game: d, saves: path.join(d, 'HikayeTurkRP', 'saves'), vendor: inst.vendorDir(LAY) };
  if (map[what]) { fs.mkdirSync(map[what], { recursive: true }); shell.openPath(map[what]); }
  return { ok: !!map[what] };
});
ipcMain.handle('copy-text', (_e, text) => { clipboard.writeText(String(text || '')); return { ok: true }; });
ipcMain.handle('open-url', (_e, url) => { if (/^https:\/\//.test(url)) shell.openExternal(url); });
ipcMain.handle('relaunch', () => { app.relaunch(); app.exit(0); });

app.whenReady().then(async () => {
  createWindow();
  if (settings().autoCheck && core.validRepo(settings().githubRepo)) {
    setTimeout(async () => {
      try { send('update-info', await core.checkUpdate(LAY, settings(), getToken())); } catch (e) { log('Güncelleme denetimi: ' + e.message); }
    }, 1500);
  }
});

app.on('window-all-closed', () => {
  // Pencere kapanınca sunucuyu bırakma: oyun açıksa sunucu çalışmaya devam etmeli
  if (server && !core.gameRunning()) core.killTree(server);
  app.quit();
});

// Launcher kapanırken save yedeği varsa geri yükle (oyun açıkken launcher kapatılırsa)
app.on('before-quit', () => {
  if (fs.existsSync(SAVE_BACKUP)) restoreGtaSaves();
});
