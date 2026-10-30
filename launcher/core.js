'use strict';
// Hikaye RP başlatıcı çekirdeği (Electron'dan bağımsız: node ile test edilebilir).
// İlke: orijinal oyun dosyalarına DOKUNULMAZ. Oyun klasörüne sadece HikayeTurkRP.asi ve HikayeTurkRP/ klasörü eklenir/kaldırılır.
const fs = require('fs');
const path = require('path');
const net = require('net');
const os = require('os');
const https = require('https');
const crypto = require('crypto');
const { spawn, execFile, execFileSync } = require('child_process');
const portablePy = require('./portable-python');

const GAME_EXE = 'GTA5.exe';
const STEAM_APP_ID = 271590;

function readJson(p, def) {
  try { return JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, '')); } catch (e) { return def; }
}
function writeJson(p, o) {
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, JSON.stringify(o, null, 2), 'utf8');
}

// "v1.2.10" > "1.2.9"
function compareVersions(a, b) {
  const pa = String(a).replace(/^v/i, '').split(/[.\-+]/).map((x) => parseInt(x, 10) || 0);
  const pb = String(b).replace(/^v/i, '').split(/[.\-+]/).map((x) => parseInt(x, 10) || 0);
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const d = (pa[i] || 0) - (pb[i] || 0);
    if (d !== 0) return d > 0 ? 1 : -1;
  }
  return 0;
}

// Program Files gibi YAZILAMAZ kurulum klasörlerinde de çalışmak için: kurulumdaki paket (bundle) kullanıcının yazılabilir
// çalışma klasörüne (%LOCALAPPDATA%\\HikayeTurkRP\\Pkg) alınır. Ayar, vendor, TR yama, güncellemeler hep orada olur.
// Çalışma klasörü bundle'dan YENİ ya da eşitse dokunulmaz (GitHub güncellemesi korunur); bundle daha yeniyse üzerine yazılır.
// Klasöre yazılabiliyor mu? (kendi kurulum klasöründe çalışmak için; Program Files gibi yerlerde false)
function isWritable(dir) {
  try {
    fs.mkdirSync(dir, { recursive: true });
    const f = path.join(dir, '.yazma_testi_' + process.pid);
    fs.writeFileSync(f, 'x');
    fs.unlinkSync(f);
    return true;
  } catch (e) { return false; }
}

const BUNDLE_ITEMS = ['mod', 'gamefiles', 'companion', 'tr-yama', 'bin', 'vendor', 'version.json', 'README.txt', 'THIRD_PARTY.md'];
function syncBundle(bundle, work, log) {
  if (path.resolve(bundle).toLowerCase() === path.resolve(work).toLowerCase()) return false;
  const bv = (readJson(path.join(bundle, 'version.json'), {}) || {}).version || '0.0.0';
  const wv = (readJson(path.join(work, 'version.json'), {}) || {}).version || '';
  const complete = ['mod', 'tr-yama', 'bin', 'vendor'].every((n) => fs.existsSync(path.join(work, n)) || !fs.existsSync(path.join(bundle, n)));
  // Aynı sürümde yeniden kurulan paket (yeni derleme): version.json'daki "build" damgası farklıysa çalışma klasörü yenilenir
  const bb = String((readJson(path.join(bundle, 'version.json'), {}) || {}).build || '');
  const wb = String((readJson(path.join(work, 'version.json'), {}) || {}).build || '');
  if (wv && compareVersions(wv, bv) >= 0 && complete && bb === wb) return false;
  fs.mkdirSync(work, { recursive: true });
  for (const name of BUNDLE_ITEMS) {
    const from = path.join(bundle, name);
    if (!fs.existsSync(from)) continue;
    fs.cpSync(from, path.join(work, name), { recursive: true, force: true });
  }
  if (log) log('Paket çalışma klasörüne alındı: ' + work + ' (sürüm ' + bv + ')');
  return true;
}

// Paket düzeni (paketlenmiş): root/{launcher,mod,gamefiles,companion,version.json}; geliştirme: proje kökü (data/, addons/, mod/build)
function layout(root) {
  const packagedAsi = path.join(root, 'mod', 'HikayeTurkRP.asi');
  const devAsi = path.join(root, 'mod', 'build', 'HikayeTurkRP.asi');
  const gf = path.join(root, 'gamefiles', 'HikayeTurkRP');
  const packaged = fs.existsSync(gf);
  return {
    root,
    asi: fs.existsSync(packagedAsi) ? packagedAsi : devAsi,
    cefDir: fs.existsSync(path.join(root, 'mod', 'cef'))
      ? path.join(root, 'mod', 'cef')
      : path.join(root, 'mod', 'build', 'cef'),
    dataDir: packaged ? path.join(gf, 'data') : path.join(root, 'data'),
    addonsDir: packaged ? path.join(gf, 'addons') : path.join(root, 'addons'),
    companionDir: path.join(root, 'companion'),
    versionFile: path.join(root, 'version.json'),
  };
}

function localVersion(lay) {
  return (readJson(lay.versionFile, {}) || {}).version || '0.0.0';
}

// ---------- Oyun klasörü ----------
function isGameDir(d) { return !!d && fs.existsSync(path.join(d, GAME_EXE)); }

function steamLibraries() {
  const libs = [];
  try {
    const out = execFileSync('reg', ['query', 'HKCU\\Software\\Valve\\Steam', '/v', 'SteamPath'], { encoding: 'utf8', windowsHide: true });
    const m = out.match(/SteamPath\s+REG_SZ\s+(.+)/);
    if (m) {
      const steam = m[1].trim().replace(/\//g, '\\');
      libs.push(steam);
      const vdf = path.join(steam, 'steamapps', 'libraryfolders.vdf');
      if (fs.existsSync(vdf)) {
        const txt = fs.readFileSync(vdf, 'utf8');
        for (const mm of txt.matchAll(/"path"\s+"([^"]+)"/g)) libs.push(mm[1].replace(/\\\\/g, '\\'));
      }
    }
  } catch (e) { /* Steam kayıt defteri yok */ }
  return libs;
}

function findGameDir(settings) {
  if (isGameDir(settings && settings.gameDir)) return settings.gameDir;
  const cands = [];
  for (const lib of steamLibraries()) cands.push(path.join(lib, 'steamapps', 'common', 'Grand Theft Auto V'));
  cands.push('C:\\Program Files (x86)\\Steam\\steamapps\\common\\Grand Theft Auto V');
  cands.push('C:\\Program Files\\Rockstar Games\\Grand Theft Auto V');
  cands.push('C:\\Program Files\\Epic Games\\GTAV');
  for (const c of cands) if (isGameDir(c)) return c;
  return '';
}

function sha1(p) {
  try { return crypto.createHash('sha1').update(fs.readFileSync(p)).digest('hex'); } catch (e) { return ''; }
}

function state(gameDir, lay) {
  const s = { gameDir, gameOk: isGameDir(gameDir), scriptHook: false, installed: false, upToDate: false, version: localVersion(lay), hasPackage: fs.existsSync(lay.asi) };
  if (!s.gameOk) return s;
  s.scriptHook = fs.existsSync(path.join(gameDir, 'ScriptHookV.dll')) && fs.existsSync(path.join(gameDir, 'dinput8.dll'));
  const gasi = path.join(gameDir, 'HikayeTurkRP.asi');
  s.installed = fs.existsSync(gasi) && fs.existsSync(path.join(gameDir, 'HikayeTurkRP', 'data'));
  s.upToDate = s.installed && s.hasPackage && sha1(gasi) === sha1(lay.asi);
  s.patch = patchState(gameDir);
  s.patch.source = !!trPatchSource(lay);
  return s;
}

// ---------- Türkçe yama (OpenIV "mods" klasörü) ----------
// Oyundaki mods/update/update.rpf ve update2.rpf, OpenIV ile hazırlanmış Türkçe yamadır (Rockstar verisi içerir, 2 GB+).
// Pakete KOYULMAZ ve değiştirilmez; başlatıcı yalnızca durumunu gösterir ve klasörü yeniden adlandırarak açıp kapatır (silmez).
// Menyoo ve OpenIV dosyalarına hiç dokunulmaz (hata ayıklama için kullanıcıda kalır).
function patchState(gameDir) {
  const on = path.join(gameDir, 'mods');
  const off = path.join(gameDir, 'mods.kapali');
  const dir = fs.existsSync(on) ? on : (fs.existsSync(off) ? off : '');
  const r = { present: !!dir, enabled: dir === on, openiv: fs.existsSync(path.join(gameDir, 'OpenIV.asi')), files: [], sizeMB: 0 };
  if (!dir) return r;
  const walk = (d) => {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
      const p = path.join(d, e.name);
      if (e.isDirectory()) walk(p);
      else { r.files.push(path.relative(dir, p)); try { r.sizeMB += fs.statSync(p).size / 1048576; } catch (x) { /* okunamadı */ } }
    }
  };
  try { walk(dir); } catch (e) { /* erişilemedi */ }
  r.sizeMB = Math.round(r.sizeMB);
  r.turkish = r.files.some((f) => /update[\\/]update2?\.rpf$/i.test(f));
  return r;
}

// Yama kaynağı: dağıtımda <kök>/tr-yama/mods (TRYama zip'leri açılınca), geliştirmede <proje>/tr yama/mods
function trPatchSource(lay) {
  for (const c of [path.join(lay.root, 'tr-yama', 'mods'), path.join(lay.root, 'tr yama', 'mods')]) {
    if (fs.existsSync(path.join(c, 'update', 'update.rpf'))) return c;
  }
  return '';
}

// ---- Türkçe yama: FARK YAMASI (xdelta3). Rockstar verisi dağıtılmaz; yamalı update.rpf/update2.rpf kullanıcının KENDİ
// orijinal dosyasından yerelde üretilir ve SHA-256 ile doğrulanır (tools/make_tr_delta.ps1 fark dosyalarını üretir).
function xdeltaPath(lay) {
  return [path.join(lay.root, 'bin', 'xdelta3.exe'), path.join(lay.root, 'tools', 'bin', 'xdelta3.exe')].find((p) => fs.existsSync(p)) || '';
}
function trManifest(lay) {
  const mp = path.join(lay.root, 'tr-yama', 'manifest.json');
  const m = readJson(mp, null);
  if (!m || !Array.isArray(m.files) || !m.files.length) return null;
  for (const f of m.files) if (!fs.existsSync(path.join(lay.root, 'tr-yama', f.delta))) return null;
  return m;
}
// Pakette Türkçe yama (fark dosyaları + xdelta3) var mı? Dönüş: manifest yolu ya da ''
function trPatchSource(lay) { return trManifest(lay) && xdeltaPath(lay) ? path.join(lay.root, 'tr-yama') : ''; }

function sha256File(p) {
  return new Promise((resolve, reject) => {
    const h = crypto.createHash('sha256');
    fs.createReadStream(p).on('data', (d) => h.update(d)).on('end', () => resolve(h.digest('hex').toUpperCase())).on('error', reject);
  });
}

function runXdelta(exe, src, delta, out) {
  return new Promise((resolve, reject) => {
    const child = spawn(exe, ['-d', '-B', '536870912', '-f', '-s', src, delta, out], { windowsHide: true });
    let err = '';
    child.stderr.on('data', (d) => { err += d.toString(); });
    child.once('error', reject);
    child.once('exit', (code) => (code === 0 ? resolve() : reject(new Error('xdelta3 başarısız (kod ' + code + '): ' + err.trim().slice(0, 200)))));
  });
}

// Türkçe yamayı oyun KOPYASINA kurur: kopyada mods yoksa üretir (mods.kapali varsa yeniden açar).
// origDir: orijinal GTA V (yalnız OKUNUR). OpenIV.asi pakette yok: mods klasörünün yüklenmesi için gerekir (uyarı verilir).
async function installTrPatch(instDir, origDir, lay, log) {
  if (!isGameDir(instDir)) throw new Error('Oyun kopyası geçersiz.');
  if (gameRunning()) throw new Error('Önce oyunu kapat.');
  const on = path.join(instDir, 'mods');
  const off = path.join(instDir, 'mods.kapali');
  const warn = [];
  if (fs.existsSync(on)) throw new Error('Kopyada zaten bir mods klasörü var; üzerine yazılmadı.');
  if (fs.existsSync(off)) {
    fs.renameSync(off, on);
    log('Kapalı duran Türkçe yama yeniden açıldı.');
  } else {
    const man = trManifest(lay);
    const xd = xdeltaPath(lay);
    if (!man || !xd) throw new Error('Pakette Türkçe yama yok (tr-yama/manifest.json ya da xdelta3.exe bulunamadı).');
    if (!origDir || !isGameDir(origDir)) throw new Error('Orijinal GTA V klasörü bulunamadı: yama onun update dosyalarından üretilir.');
    const dstDir = path.join(on + '.hazirlaniyor', 'update');
    fs.rmSync(on + '.hazirlaniyor', { recursive: true, force: true });
    fs.mkdirSync(dstDir, { recursive: true });
    try {
      for (const f of man.files) {
        const src = path.join(origDir, 'update', f.name);
        if (!fs.existsSync(src)) throw new Error('Orijinal dosya yok: update/' + f.name);
        if (fs.statSync(src).size !== f.src_size) {
          throw new Error('GTA V sürümü yamayla uyuşmuyor (update/' + f.name + ' boyutu farklı; yama oyun ' + (man.game_version || '?') + ' içindir). Yama yeni sürüme uyarlanmalı.');
        }
        log('Türkçe yama üretiliyor: ' + f.name + ' (orijinalden, birkaç dakika sürebilir)...');
        const tmp = path.join(dstDir, f.name + '.tmp');
        await runXdelta(xd, src, path.join(lay.root, 'tr-yama', f.delta), tmp);
        if (fs.statSync(tmp).size !== f.out_size || (await sha256File(tmp)) !== String(f.out_sha256).toUpperCase()) {
          throw new Error('Üretilen ' + f.name + ' doğrulanamadı (SHA-256 uyuşmuyor): orijinal dosya bozuk ya da farklı sürüm olabilir.');
        }
        fs.renameSync(tmp, path.join(dstDir, f.name));
      }
      fs.renameSync(on + '.hazirlaniyor', on);
    } catch (e) {
      fs.rmSync(on + '.hazirlaniyor', { recursive: true, force: true });
      throw e;
    }
    log('Türkçe yama kuruldu.');
  }
  if (!fs.existsSync(path.join(instDir, 'OpenIV.asi'))) {
    warn.push('OpenIV.asi oyun kopyasında yok: mods klasörünün yüklenmesi için gerekir (openiv.com, "ScriptHookV / OpenIV ekle" ile eklenir).');
  }
  return warn;
}

function togglePatch(gameDir, enable) {
  if (gameRunning()) throw new Error('Önce oyunu kapat.');
  const on = path.join(gameDir, 'mods');
  const off = path.join(gameDir, 'mods.kapali');
  if (enable && !fs.existsSync(on) && fs.existsSync(off)) fs.renameSync(off, on);
  else if (!enable && fs.existsSync(on) && !fs.existsSync(off)) fs.renameSync(on, off);
  else throw new Error('Yama klasörü bu durumda değiştirilemez (mods / mods.kapali).');
  return patchState(gameDir);
}

// ---------- Kur / kaldır ----------
function copyDir(src, dst) { fs.cpSync(src, dst, { recursive: true, force: true }); }


// ---------- Python sunucusu ----------
function portOpen(port, host) {
  return new Promise((resolve) => {
    const s = net.connect({ port, host: host || '127.0.0.1' });
    s.once('connect', () => { s.destroy(); resolve(true); });
    s.once('error', () => resolve(false));
    s.setTimeout(800, () => { s.destroy(); resolve(false); });
  });
}

async function waitPort(port, timeoutMs, isAlive) {
  const t0 = Date.now();
  while (Date.now() - t0 < timeoutMs) {
    if (await portOpen(port)) return true;
    if (isAlive && !isAlive()) return false;
    await new Promise((r) => setTimeout(r, 1000));
  }
  return false;
}

function startServer(lay, python, onLog, extraEnv) {
  const script = path.join(lay.companionDir, 'server.py');
  if (!fs.existsSync(script)) throw new Error('companion/server.py bulunamadı.');
  let py = python;
  if (!py || py === 'python') py = portablePy.installed() ? portablePy.exe() : 'python';
  const child = spawn(py, ['server.py'], {
    cwd: lay.companionDir,
    env: Object.assign({}, process.env, {
      PYTHONIOENCODING: 'utf-8',
      PYTHONUNBUFFERED: '1',
      PYTHONPATH: lay.companionDir,
      HIKAYE_WEBV2_DATA: lay.dataDir,
    }, extraEnv || {}),
    windowsHide: true,
  });
  const pipe = (stream) => stream.on('data', (d) => d.toString('utf8').split(/\r?\n/).filter(Boolean).forEach((l) => onLog(l)));
  pipe(child.stdout); pipe(child.stderr);
  child.alive = true;
  child.once('exit', () => { child.alive = false; });
  child.once('error', (e) => { child.alive = false; onLog('Sunucu başlatılamadı: ' + e.message); });
  return child;
}

function killTree(child) {
  if (!child || !child.pid) return;
  try { execFileSync('taskkill', ['/PID', String(child.pid), '/T', '/F'], { windowsHide: true, stdio: 'ignore' }); } catch (e) { /* zaten kapalı */ }
}

function checkPython(python) {
  // Sadece taşınabilir Python veya ayarlardan verilen yol kullanılır (sistem Python'u ARANMAZ).
  var py = python;
  if (!py || py === 'python') py = portablePy.installed() ? portablePy.exe() : '';
  if (!py) return '';
  try { return execFileSync(py, ['--version'], { encoding: 'utf8', windowsHide: true, stdio: ['pipe','pipe','pipe'] }).trim(); } catch (e) { return ''; }
}

// ---------- Otomatik hazırlık yardımcıları ----------
const PY_IMPORTS = 'import torch, websockets, yaml, faster_whisper, sounddevice, numpy, httpx, pyautogui, jinja2';
const PY_XTTS_OK = 'from TTS.api import TTS';

function pythonPackagesOk(python) {
  var py = python;
  if (!py || py === 'python') py = portablePy.installed() ? portablePy.exe() : '';
  if (!py) return false;
  try { execFileSync(py, ['-c', PY_IMPORTS], { windowsHide: true, stdio: 'ignore' }); return true; } catch (e) { return false; }
}

// pip ile sunucu paketlerini kurar (uzun sürer); çıktı satır satır onLog'a akar.
// Temel paketler (requirements-full.txt) kurulduktan sonra XTTS (coqui-tts) opsiyonel olarak denenir.
// coqui-tts Python <=3.14 gerektirir; uyumsuz sürümde başarısız olur ama hata ATMAZ.
function pipInstall(python, companionDir, onLog) {
  return new Promise((resolve, reject) => {
    const req = path.join(companionDir, 'requirements-full.txt');
    if (!fs.existsSync(req)) return reject(new Error('requirements-full.txt bulunamadı.'));
    let py = python;
    if (!py || py === 'python') py = portablePy.installed() ? portablePy.exe() : '';
    if (!py) return reject(new Error('Python yok: önce taşınabilir Python kurulmalı.'));
    const env = Object.assign({}, process.env, { PYTHONIOENCODING: 'utf-8' });
    const pipe = (s) => s.on('data', (d) => d.toString('utf8').split(/\r?\n/).filter(Boolean).forEach((l) => onLog(l)));
    const child = spawn(py, ['-m', 'pip', 'install', '-r', req, '--no-warn-script-location', '--disable-pip-version-check'], { windowsHide: true, env });
    pipe(child.stdout); pipe(child.stderr);
    child.once('error', reject);
    child.once('exit', (code) => {
      if (code !== 0) return reject(new Error('pip kurulumu başarısız (kod ' + code + ')'));
      // Temel paketler kuruldu; XTTS (coqui-tts) opsiyonel
      const xttsReq = path.join(companionDir, 'requirements-xtts.txt');
      if (!fs.existsSync(xttsReq)) return resolve();
      onLog('XTTS (coqui-tts) kuruluyor (opsiyonel — başarısız olursa Piper TTS yedek devreye girer)...');
      const xtts = spawn(py, ['-m', 'pip', 'install', '-r', xttsReq, '--no-warn-script-location', '--disable-pip-version-check'], { windowsHide: true, env });
      pipe(xtts.stdout); pipe(xtts.stderr);
      xtts.once('error', () => { onLog('XTTS kurulamadı: Piper TTS yedek olarak kullanılacak.'); resolve(); });
      xtts.once('exit', (c) => {
        if (c !== 0) onLog('XTTS (coqui-tts) kurulamadı (Python sürümü uyumsuz olabilir): Piper TTS yedek motor olarak kullanılacak.');
        else onLog('XTTS (coqui-tts) kuruldu.');
        resolve();  // opsiyonel: başarısız olsa bile devam et
      });
    });
  });
}

// Taşınabilir Python: sistemde Python olmasa bile çalışır
async function ensurePortablePython(onLog) {
  const pyExe = await portablePy.setup(onLog);
  return pyExe;
}

function wingetAvailable() {
  try { execFileSync('winget', ['--version'], { windowsHide: true, stdio: 'ignore' }); return true; } catch (e) { return false; }
}

// winget varsa Python'u sessizce kurar (kullanıcı bazında, yönetici izni istemez)
function installPythonWinget(onLog) {
  return new Promise((resolve, reject) => {
    const child = spawn('winget', ['install', '-e', '--id', 'Python.Python.3.12', '--scope', 'user', '--silent', '--accept-package-agreements', '--accept-source-agreements'], { windowsHide: true });
    const pipe = (s) => s.on('data', (d) => d.toString('utf8').split(/\r?\n/).map((x) => x.trim()).filter((x) => x && !/^[-\\|/]$/.test(x)).forEach((l) => onLog(l)));
    pipe(child.stdout); pipe(child.stderr);
    child.once('error', reject);
    child.once('exit', (code) => (code === 0 ? resolve() : reject(new Error('winget Python kurulumu başarısız (kod ' + code + ')'))));
  });
}

// Üçüncü taraf dosyaları (ScriptHookV.dll, dinput8.dll, OpenIV.asi) bilgisayarda OTOMATİK bulur ve vendor'a alır
// (orijinal GTA'ya DEĞİL). Bakılan yerler (yalnız OKUNUR): İndirilenler (ScriptHookV zip'i ve tek tek dosyalar), Masaüstü,
// OpenIV kurulum klasörleri, varsa orijinal GTA V klasörü. İnternetten indirmez: dev-c.com otomatik indirmeyi engeller ve
// OpenIV.asi için sabit bir indirme adresi yoktur (OpenIV'in ASI Yöneticisi ile gelir).
const VENDOR_NAMES = ['ScriptHookV.dll', 'dinput8.dll', 'OpenIV.asi'];

function importVendorFromDownloads(vendorDir, log, origDir) {
  const home = process.env.USERPROFILE || os.homedir();
  const dirs = [path.join(home, 'Downloads'), path.join(home, 'Desktop'), path.join(process.env.LOCALAPPDATA || '', 'OpenIV'),
    path.join(process.env.APPDATA || '', 'OpenIV'), 'C:\\Program Files\\OpenIV', 'C:\\Program Files (x86)\\OpenIV'];
  if (origDir) dirs.push(origDir);
  const got = [];
  fs.mkdirSync(vendorDir, { recursive: true });
  const take = (name, from) => {
    if (got.includes(name) || fs.existsSync(path.join(vendorDir, name))) return;
    try { fs.copyFileSync(from, path.join(vendorDir, name)); got.push(name); log('  ' + name + ' bulundu: ' + from); } catch (e) { /* okunamadı */ }
  };
  for (const d of dirs) {
    if (!d || !fs.existsSync(d)) continue;
    let entries = [];
    try { entries = fs.readdirSync(d, { withFileTypes: true }); } catch (e) { continue; }
    // tek tek dosyalar (bu klasörde ve bir alt düzeyde)
    for (const n of VENDOR_NAMES) {
      const hit = entries.find((e) => e.isFile() && e.name.toLowerCase() === n.toLowerCase());
      if (hit) take(n, path.join(d, hit.name));
    }
    for (const sub of entries.filter((e) => e.isDirectory() && /openiv|scripthook|asi|bin/i.test(e.name))) {
      let inner = [];
      try { inner = fs.readdirSync(path.join(d, sub.name), { withFileTypes: true }); } catch (e) { continue; }
      for (const n of VENDOR_NAMES) {
        const hit = inner.find((e) => e.isFile() && e.name.toLowerCase() === n.toLowerCase());
        if (hit) take(n, path.join(d, sub.name, hit.name));
      }
    }
    // ScriptHookV zip'i (SDK değil): bin\ScriptHookV.dll + bin\dinput8.dll
    if (!fs.existsSync(path.join(vendorDir, 'ScriptHookV.dll')) || !fs.existsSync(path.join(vendorDir, 'dinput8.dll'))) {
      const zip = entries.filter((e) => e.isFile() && /^ScriptHookV.*\.zip$/i.test(e.name) && !/sdk/i.test(e.name)).map((e) => e.name).sort().pop();
      if (zip) {
        const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'hikaye_shv_'));
        try {
          expandZip(path.join(d, zip), tmp);
          for (const f of ['ScriptHookV.dll', 'dinput8.dll']) {
            const hit = [path.join(tmp, 'bin', f), path.join(tmp, f)].find((p) => fs.existsSync(p));
            if (hit) take(f, hit);
          }
        } finally { fs.rmSync(tmp, { recursive: true, force: true }); }
      }
    }
  }
  return got;
}

// ---------- Oyun ----------
let runningProbe = null;  // testlerde ana makinenin durumundan bağımsız olmak için
function setGameRunningProbe(f) { runningProbe = f; }
function gameRunning() {
  if (runningProbe) return !!runningProbe();
  try {
    const out = execFileSync('tasklist', ['/FI', 'IMAGENAME eq ' + GAME_EXE, '/NH'], { encoding: 'utf8', windowsHide: true });
    return out.toLowerCase().includes(GAME_EXE.toLowerCase());
  } catch (e) { return false; }
}

// ---------- GitHub güncelleme ----------
function httpGet(url, opts, redirects) {
  return new Promise((resolve, reject) => {
    const u = new URL(url);
    const headers = Object.assign({ 'User-Agent': 'HikayeTurkRP-Launcher', Accept: 'application/vnd.github+json' }, (opts && opts.headers) || {});
    const req = https.get({ hostname: u.hostname, path: u.pathname + u.search, headers }, (res) => {
      if ([301, 302, 303, 307, 308].includes(res.statusCode) && res.headers.location && (redirects || 0) < 6) {
        res.resume();
        const next = new URL(res.headers.location, url);
        const h2 = Object.assign({}, (opts && opts.headers) || {});
        if (next.hostname !== u.hostname) delete h2.Authorization;  // anahtarı başka sunucuya sızdırma
        return resolve(httpGet(next.toString(), Object.assign({}, opts, { headers: h2 }), (redirects || 0) + 1));
      }
      resolve(res);
    });
    req.on('error', reject);
    req.setTimeout(30000, () => req.destroy(new Error('Zaman aşımı')));
  });
}

async function httpJson(url, token) {
  const res = await httpGet(url, { headers: token ? { Authorization: 'Bearer ' + token } : {} });
  const chunks = [];
  for await (const c of res) chunks.push(c);
  const body = Buffer.concat(chunks).toString('utf8');
  if (res.statusCode !== 200) {
    let msg = '';
    try { msg = JSON.parse(body).message || ''; } catch (e) { /* düz metin */ }
    throw new Error('GitHub ' + res.statusCode + (msg ? ': ' + msg : ''));
  }
  return JSON.parse(body);
}

function validRepo(repo) {
  // GitHub: kullanıcı adı harf/rakam/tire; depo adı harf/rakam/_ . - ('.' ve '..' olamaz)
  const m = /^([A-Za-z0-9](?:[A-Za-z0-9-]*))\/([A-Za-z0-9_.-]+)$/.exec(repo || '');
  return !!m && m[2] !== '.' && m[2] !== '..';
}

async function checkUpdate(lay, settings, token) {
  if (!validRepo(settings.githubRepo)) return { ok: false, reason: 'GitHub deposu ayarlanmadı (Ayarlar > kullanici/depo).' };
  const rel = await httpJson('https://api.github.com/repos/' + settings.githubRepo + '/releases/latest', token);
  const cur = localVersion(lay);
  const latest = String(rel.tag_name || '').replace(/^v/i, '');
  const asset = (rel.assets || []).find((a) => /\.zip$/i.test(a.name));
  return {
    ok: true, current: cur, latest, available: compareVersions(latest, cur) > 0,
    notes: rel.body || '', page: rel.html_url || '', published: rel.published_at || '',
    asset: asset ? { name: asset.name, size: asset.size, api: asset.url, download: asset.browser_download_url } : null,
  };
}

async function downloadAsset(info, token, destFile, onProgress) {
  const url = token ? info.asset.api : info.asset.download;
  const headers = token ? { Authorization: 'Bearer ' + token, Accept: 'application/octet-stream' } : { Accept: 'application/octet-stream' };
  const res = await httpGet(url, { headers });
  if (res.statusCode !== 200) throw new Error('İndirme başarısız: HTTP ' + res.statusCode);
  const total = parseInt(res.headers['content-length'] || '0', 10) || info.asset.size || 0;
  fs.mkdirSync(path.dirname(destFile), { recursive: true });
  const out = fs.createWriteStream(destFile);
  let got = 0;
  res.on('data', (c) => { got += c.length; if (onProgress && total) onProgress(Math.round((got * 100) / total)); });
  await new Promise((resolve, reject) => { res.pipe(out); out.on('finish', resolve); out.on('error', reject); res.on('error', reject); });
  return destFile;
}

function expandZip(zip, dest) {
  fs.rmSync(dest, { recursive: true, force: true });
  fs.mkdirSync(dest, { recursive: true });
  // Yollar ortam değişkeniyle verilir (komut satırında boşluk/özel karakter sorunu olmasın)
  execFileSync('powershell', ['-NoProfile', '-Command', 'Expand-Archive -LiteralPath $env:HK_ZIP -DestinationPath $env:HK_DEST -Force'],
    { windowsHide: true, env: Object.assign({}, process.env, { HK_ZIP: zip, HK_DEST: dest }) });
}


// ---------- Eklenti (addon) zip'i: yükle / listele / kaldır ----------
// Eklentiler <root>/user-addons/<kimlik> altında saklanır (oyun kopyası silinse de kalır) ve her kopya hazırlanışında kopyaya konur.
// Zip içeriği: addon.json (id, name, version) + shops/ items.json vehicles.json missions/ web/ (sadece veri dosyaları: json, hml, hss, md, txt).
// Çalıştırılabilir dosya (exe, dll, asi, bat, ps1, py...) içeren zip REDDEDİLİR.
const ADDON_BAD_EXT = new Set(['.exe', '.dll', '.asi', '.bat', '.cmd', '.ps1', '.vbs', '.js', '.jar', '.msi', '.scr', '.com', '.lnk', '.py', '.sh', '.reg', '.cpl', '.hta', '.psm1', '.wsf']);
const ADDON_OK_EXT = new Set(['.json', '.hml', '.hss', '.md', '.txt']);
const ADDON_TOP = new Set(['addon.json', 'shops', 'items.json', 'vehicles.json', 'missions', 'web']);
const ADDON_MAX_BYTES = 40 * 1024 * 1024;

function userAddonsDir(lay) { return path.join(lay.root, 'user-addons'); }

function listAddons(lay) {
  const base = userAddonsDir(lay);
  if (!fs.existsSync(base)) return [];
  const out = [];
  for (const e of fs.readdirSync(base, { withFileTypes: true })) {
    if (!e.isDirectory()) continue;
    const meta = readJson(path.join(base, e.name, 'addon.json'), {}) || {};
    out.push({ id: e.name, name: meta.name || e.name, version: meta.version || '', enabled: meta.enabled !== false });
  }
  return out.sort((a, b) => a.id.localeCompare(b.id));
}

function walkFiles(dir, rel = '') {
  const out = [];
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const r = rel ? rel + '/' + e.name : e.name;
    if (e.isDirectory()) out.push(...walkFiles(path.join(dir, e.name), r));
    else out.push(r);
  }
  return out;
}

// zip içindeki yolları çıkarmadan önce denetle (../ ya da mutlak yol: zip-slip)
function zipEntryNames(zip) {
  const script = "Add-Type -AssemblyName System.IO.Compression.FileSystem; $z=[IO.Compression.ZipFile]::OpenRead($env:HK_ZIP); try { $z.Entries | ForEach-Object { $_.FullName } } finally { $z.Dispose() }";
  const out = execFileSync('powershell', ['-NoProfile', '-Command', script], { windowsHide: true, encoding: 'utf8', env: Object.assign({}, process.env, { HK_ZIP: zip }) });
  return out.split(/\r?\n/).filter(Boolean);
}

function checkMissionJson(rel, j, warnings) {
  if (!j || typeof j !== 'object') { warnings.push(rel + ': görev nesnesi değil'); return; }
  for (const k of ['id', 'giver', 'npcs', 'stages']) if (!(k in j)) warnings.push(rel + ': zorunlu alan yok: ' + k);
  if (j.npcs && j.giver && !(j.giver in j.npcs)) warnings.push(rel + ': giver "' + j.giver + '" npcs içinde yok');
  const types = new Set(['talk', 'interact', 'vehicle', 'drive_to', 'walk', 'scene', 'install', 'escort']);
  for (const s of (Array.isArray(j.stages) ? j.stages : [])) {
    const t = s.type || 'talk';
    if (!types.has(t)) warnings.push(rel + ': bilinmeyen aşama türü "' + t + '"');
    if (['talk', 'install', 'escort', 'walk'].includes(t) && !s.npc) warnings.push(rel + ': npc yok: ' + (s.id || '?'));
    if (['interact', 'install', 'escort', 'scene'].includes(t) && !Array.isArray(s.pos)) warnings.push(rel + ': pos yok: ' + (s.id || '?'));
  }
}

function installAddonZip(zip, lay, instDir, log) {
  if (!zip || !fs.existsSync(zip)) throw new Error('Zip dosyası bulunamadı.');
  if (!/\.zip$/i.test(zip)) throw new Error('Sadece .zip eklenti paketi yüklenebilir.');
  for (const n of zipEntryNames(zip)) {
    if (/^([a-zA-Z]:|[\\/])/.test(n) || n.split(/[\\/]/).includes('..')) throw new Error('Zip güvensiz bir yol içeriyor: ' + n);
  }
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'hikaye_addon_'));
  try {
    expandZip(zip, tmp);
    // addon.json: kökte ya da tek bir üst klasörün içinde
    let aroot = '';
    if (fs.existsSync(path.join(tmp, 'addon.json'))) aroot = tmp;
    else {
      const subs = fs.readdirSync(tmp, { withFileTypes: true }).filter((e) => e.isDirectory() && fs.existsSync(path.join(tmp, e.name, 'addon.json')));
      if (subs.length === 1) aroot = path.join(tmp, subs[0].name);
    }
    if (!aroot) throw new Error('Pakette addon.json bulunamadı (kökte ya da tek üst klasörde olmalı).');
    const meta = readJson(path.join(aroot, 'addon.json'), null);
    if (!meta || typeof meta !== 'object') throw new Error('addon.json bozuk (geçerli JSON değil).');
    const id = String(meta.id || '');
    if (!/^[a-z0-9][a-z0-9_-]{1,40}$/i.test(id) || id.startsWith('_')) throw new Error('addon.json "id" geçersiz: harf/rakam/_/- , 2-41 karakter, "_" ile başlayamaz.');
    const files = walkFiles(aroot);
    let total = 0;
    const warnings = [];
    for (const rel of files) {
      const ext = path.extname(rel).toLowerCase();
      if (ADDON_BAD_EXT.has(ext)) throw new Error('Çalıştırılabilir/betik dosya içeriyor, reddedildi: ' + rel);
      if (!ADDON_OK_EXT.has(ext)) throw new Error('İzin verilmeyen dosya türü (' + (ext || 'uzantısız') + '): ' + rel + ' — sadece json, hml, hss, md, txt');
      total += fs.statSync(path.join(aroot, rel)).size;
      const top = rel.split('/')[0];
      if (!ADDON_TOP.has(top) && !/^readme/i.test(top)) warnings.push('Bilinmeyen öğe yoksayılır: ' + top);
      if (ext === '.json') {
        let j;
        try { j = JSON.parse(fs.readFileSync(path.join(aroot, rel), 'utf8').replace(/^\uFEFF/, '')); } catch (e) { throw new Error('JSON bozuk: ' + rel + ' (' + e.message + ')'); }
        if (rel.startsWith('missions/')) checkMissionJson(rel, j, warnings);
      }
    }
    if (total > ADDON_MAX_BYTES) throw new Error('Eklenti çok büyük (en fazla 40 MB).');
    const dest = path.join(userAddonsDir(lay), id);
    const replaced = fs.existsSync(dest);
    fs.rmSync(dest, { recursive: true, force: true });
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    fs.cpSync(aroot, dest, { recursive: true });
    // oyun kopyası varsa hemen oraya da konur (yoksa kopya hazırlanınca konur)
    let inInstance = false;
    if (instDir && fs.existsSync(path.join(instDir, 'HikayeTurkRP'))) {
      const idest = path.join(instDir, 'HikayeTurkRP', 'addons', id);
      fs.rmSync(idest, { recursive: true, force: true });
      fs.mkdirSync(path.dirname(idest), { recursive: true });
      fs.cpSync(dest, idest, { recursive: true });
      inInstance = true;
    }
    if (log) log('Eklenti ' + (replaced ? 'güncellendi' : 'yüklendi') + ': ' + id + ' (' + files.length + ' dosya)');
    return { id, name: meta.name || id, version: meta.version || '', files: files.length, replaced, inInstance, warnings };
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
}

function removeAddon(id, lay, instDir, log) {
  if (!/^[a-z0-9][a-z0-9_-]{1,40}$/i.test(String(id || '')) || String(id).startsWith('_')) throw new Error('Geçersiz eklenti kimliği.');
  fs.rmSync(path.join(userAddonsDir(lay), id), { recursive: true, force: true });
  if (instDir) fs.rmSync(path.join(instDir, 'HikayeTurkRP', 'addons', id), { recursive: true, force: true });
  if (log) log('Eklenti kaldırıldı: ' + id);
}

// İndirilen paketi uygular: root altındaki mod/gamefiles/companion/version.json güncellenir; launcher/ ve ayar dosyaları korunur.
function applyUpdate(zip, root, log) {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'hikaye_upd_'));
  expandZip(zip, tmp);
  let src = tmp;
  if (!fs.existsSync(path.join(src, 'version.json'))) {
    const sub = fs.readdirSync(tmp, { withFileTypes: true }).find((e) => e.isDirectory() && fs.existsSync(path.join(tmp, e.name, 'version.json')));
    if (!sub) throw new Error('Pakette version.json yok: geçersiz güncelleme.');
    src = path.join(tmp, sub.name);
  }
  for (const e of fs.readdirSync(src, { withFileTypes: true })) {
    if (e.name === 'launcher' || e.name === 'launcher-settings.json') continue;
    log('Güncelleniyor: ' + e.name);
    fs.cpSync(path.join(src, e.name), path.join(root, e.name), { recursive: true, force: true });
  }
  fs.rmSync(tmp, { recursive: true, force: true });
  return (readJson(path.join(root, 'version.json'), {}) || {}).version || '?';
}

module.exports = {
  GAME_EXE, STEAM_APP_ID, readJson, writeJson, compareVersions, layout, localVersion, isGameDir, findGameDir, state,
  installAddonZip, listAddons, removeAddon, userAddonsDir,
  isWritable, syncBundle, PY_IMPORTS, PY_XTTS_OK, pythonPackagesOk, pipInstall, wingetAvailable, installPythonWinget, ensurePortablePython, portablePy, importVendorFromDownloads, setGameRunningProbe, patchState, togglePatch, trPatchSource, installTrPatch, portOpen, waitPort, startServer, killTree, checkPython, gameRunning, checkUpdate, downloadAsset, applyUpdate, validRepo, copyDir,
};
