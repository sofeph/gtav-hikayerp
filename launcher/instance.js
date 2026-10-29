'use strict';
// FiveM mantığı: mod, ORİJİNAL GTA V klasörüne HİÇ dokunmadan kendi oyun kopyasından (instance) çalışır.
// Orijinal klasör yalnızca OKUNUR. Kopya "yalın" tutulur (dosya kalabalığı yok):
//   - büyük dosyalar (.rpf) ve klasörler (update, x64) orijinalden BAĞLANIR: önce sembolik bağ, olmazsa sabit bağ (hard link, aynı sürücüde),
//     ikisi de olmazsa büyük dosya KOPYALANMAZ (30+ GB) - kullanıcıya Geliştirici Modu / aynı sürücü önerilir;
//   - oyunun çalışması için gereken küçük dosyalar (GTA5.exe, DLL'ler, sürüm dosyaları) kopyalanır, orijinal güncellenince yenilenir;
//   - kurulum/oyun ihtiyacı olmayanlar (BattlEye, Redistributables, PlayGTAV, loglar, vdf) hiç oluşturulmaz.
// Üçüncü taraf dosyalar (ScriptHookV.dll, dinput8.dll, OpenIV.asi) <kök>/vendor içinde tutulur; kopyaya konur, orijinale ASLA.
const fs = require('fs');
const path = require('path');
const os = require('os');

const cfg = { linkMinBytes: 16 * 1024 * 1024 };  // bundan büyük (exe/dll hariç) dosyalar bağlanır
const LINK_DIRS = ['update', 'x64'];               // klasörler: bağlantı (junction) - yönetici izni gerekmez
// GTA5.exe doğrudan çalıştırılırsa 'ERR_NO_LAUNCHER' verir: oyun PlayGTAV.exe (Steam başlatıcısı) üzerinden açılmalı, bu yüzden başlatıcı exe'leri de kopyalanır.
const COPY_RE = /^(GTA5\.exe|PlayGTAV\.exe|GTAVLauncher\.exe|GTAVLanguageSelect\.exe|.*\.dll)$/i;  // her zaman kopyalanır (oyun bunları kendi klasöründen yükler)
const COPY_EXTRA = new Set(['version.txt', 'versioninfo.txt', 'title.rgl'].map((s) => s.toLowerCase()));
const DEFAULT_ARGS = '-scOfflineOnly -nobattleye';  // BattlEye kapalı + Social Club çevrimdışı (tek oyunculu mod için)
const SKIP_DLL = /^(ScriptHookV|dinput8|OpenIV)/i; // üçüncü taraf: orijinalde kalmış olsa da kopyaya BURADAN alınmaz (vendor'dan gelir)
const VENDOR_FILES = ['ScriptHookV.dll', 'dinput8.dll', 'OpenIV.asi', 'xinput1_4.dll'];   // xinput1_4.dll isteğe bağlı
const OWN = new Set(['commandline.txt', 'hikayeturkrp.asi', 'hikayeturkrp', 'mods', 'mods.kapali', 'steam_appid.txt', 'scripthookv.dll', 'dinput8.dll', 'openiv.asi']);

function defaultInstanceDir() {
  return path.join(process.env.LOCALAPPDATA || path.join(os.homedir(), 'AppData', 'Local'), 'HikayeTurkRP', 'Game');
}
function vendorDir(lay) { return path.join(lay.root, 'vendor'); }

function sameFile(a, b) {
  try { const x = fs.statSync(a), y = fs.statSync(b); return x.ino === y.ino && x.dev === y.dev && x.ino !== 0; } catch (e) { return false; }
}
function sameCopy(a, b) {
  try { const x = fs.statSync(a), y = fs.statSync(b); return x.size === y.size && Math.abs(x.mtimeMs - y.mtimeMs) < 2000; } catch (e) { return false; }
}

// Büyük dosya bağı: sembolik (Geliştirici Modu/yönetici gerekir) -> sabit bağ (aynı sürücü). Kopya YOK.
function linkBig(src, dst) {
  if (fs.existsSync(dst) || isLink(dst)) removeLink(dst);
  try { fs.symlinkSync(src, dst, 'file'); return 'symlink'; } catch (e) { if (e.code !== 'EPERM' && e.code !== 'EXDEV' && e.code !== 'ENOENT') throw e; }
  try { fs.linkSync(src, dst); return 'hardlink'; } catch (e) {
    if (e.code === 'EXDEV') throw new Error('Oyun kopyası orijinal oyunla farklı sürücüde ve sembolik bağ izni yok. Windows "Geliştirici Modu"nu aç (Ayarlar > Gizlilik ve güvenlik > Geliştiriciler) ya da oyun kopyası klasörünü orijinalle aynı sürücüye al.');
    throw e;
  }
}
function isLink(p) { try { return fs.lstatSync(p).isSymbolicLink(); } catch (e) { return false; } }
// Bağı güvenle kaldır: hedef (orijinal) içeriğine DOKUNMAZ
function removeLink(p) {
  try {
    const st = fs.lstatSync(p);
    if (st.isSymbolicLink()) { try { fs.unlinkSync(p); } catch (e) { fs.rmdirSync(p); } }  // dosya bağı: unlink; junction: rmdir (yalnız bağı siler)
    else if (st.isFile()) fs.unlinkSync(p);
  } catch (e) { /* yok */ }
}

function isInstance(dir) { return !!dir && fs.existsSync(path.join(dir, 'GTA5.exe')) && fs.existsSync(path.join(dir, 'x64a.rpf')); }

// Kopyayı orijinale göre kur/yenile (idempotent). Dönüş: {linked, copied, method, warnings}
function sync(origDir, instDir, lay, log, opts) {
  if (!fs.existsSync(path.join(origDir, 'GTA5.exe'))) throw new Error('Orijinal oyun klasörü geçersiz.');
  if (path.resolve(instDir).toLowerCase().startsWith(path.resolve(origDir).toLowerCase() + path.sep)) throw new Error('Oyun kopyası orijinal klasörün İÇİNDE olamaz.');
  fs.mkdirSync(instDir, { recursive: true });
  const res = { linked: 0, copied: 0, method: '', warnings: [] };
  for (const e of fs.readdirSync(origDir, { withFileTypes: true })) {
    const name = e.name, low = name.toLowerCase();
    const src = path.join(origDir, name), dst = path.join(instDir, name);
    if (OWN.has(low) || SKIP_DLL.test(name)) continue;
    if (e.isDirectory()) {
      if (!LINK_DIRS.includes(low)) continue;                       // gerekmeyen klasörler (BattlEye, Redistributables, menyooStuff...) hiç oluşturulmaz
      if (isLink(dst)) { const t = fs.readlinkSync(dst); if (path.resolve(t).toLowerCase() === path.resolve(src).toLowerCase()) continue; removeLink(dst); }
      if (fs.existsSync(dst)) continue;                              // gerçek klasör: elleme
      fs.symlinkSync(src, dst, 'junction');
      res.linked++;
      continue;
    }
    if (!e.isFile()) continue;
    const size = fs.statSync(src).size;
    if (COPY_RE.test(name) || COPY_EXTRA.has(low)) {
      if (!sameCopy(src, dst)) { fs.copyFileSync(src, dst); fs.utimesSync(dst, new Date(), fs.statSync(src).mtime); res.copied++; }
    } else if (/\.rpf$/i.test(name) || size >= cfg.linkMinBytes) {
      if (sameFile(src, dst) || (isLink(dst) && fs.existsSync(dst) && path.resolve(fs.readlinkSync(dst)).toLowerCase() === path.resolve(src).toLowerCase())) continue;
      res.method = linkBig(src, dst);
      res.linked++;
    }
  }
  // Steam sürücü gerektirir: steam_appid.txt kopyada (orijinalde değil)
  fs.writeFileSync(path.join(instDir, 'steam_appid.txt'), '271590');
  // Başlatma parametreleri: kopyanın kendi commandline.txt'i (orijinale yazılmaz). Boşsa varsayılan: BattlEye kapalı, Social Club çevrimdışı.
  const args = ((opts && opts.args) || DEFAULT_ARGS).replace(/[\r\n]+/g, ' ').trim();
  fs.writeFileSync(path.join(instDir, 'commandline.txt'), args + '\r\n');
  // Üçüncü taraf dosyalar: vendor'dan
  const vd = vendorDir(lay);
  for (const f of VENDOR_FILES) {
    const s = path.join(vd, f);
    if (fs.existsSync(s)) { if (!sameCopy(s, path.join(instDir, f))) fs.copyFileSync(s, path.join(instDir, f)); }
    else if (f !== 'OpenIV.asi' && f !== 'xinput1_4.dll') res.warnings.push(f + ' vendor klasöründe yok (ayrıca eklenmeli).');
  }
  // Mod: asi + runtime (kayıtlara dokunmaz)
  if (!fs.existsSync(lay.asi)) throw new Error('Pakette HikayeTurkRP.asi yok.');
  fs.copyFileSync(lay.asi, path.join(instDir, 'HikayeTurkRP.asi'));
  const rt = path.join(instDir, 'HikayeTurkRP');
  fs.mkdirSync(path.join(rt, 'saves'), { recursive: true });
  fs.cpSync(lay.dataDir, path.join(rt, 'data'), { recursive: true, force: true });
  if (fs.existsSync(lay.addonsDir)) {
    const adst = path.join(rt, 'addons');
    fs.mkdirSync(adst, { recursive: true });
    for (const e of fs.readdirSync(lay.addonsDir, { withFileTypes: true })) {
      const from = path.join(lay.addonsDir, e.name), to = path.join(adst, e.name);
      if (!e.isDirectory() || e.name.startsWith('_') || !fs.existsSync(to)) fs.cpSync(from, to, { recursive: true, force: true });
    }
  }
  {   // launcher'dan yüklenen eklentiler (<root>/user-addons): kopya silinse de kalır; her hazırlıkta kopyaya konur
    const ua = path.join(lay.root, 'user-addons');
    if (fs.existsSync(ua)) {
      const adst = path.join(rt, 'addons');
      fs.mkdirSync(adst, { recursive: true });
      for (const e of fs.readdirSync(ua, { withFileTypes: true })) {
        if (e.isDirectory() && !e.name.startsWith('_')) fs.cpSync(path.join(ua, e.name), path.join(adst, e.name), { recursive: true, force: true });
      }
    }
  }
  log('Oyun kopyası hazır: ' + res.linked + ' bağ, ' + res.copied + ' kopya' + (res.method ? ' (' + res.method + ')' : ''));
  return res;
}

// Kopyayı sil: bağlar yalnız bağ olarak kaldırılır (orijinal içerik korunur). keepSaves: HikayeTurkRP/saves yedeklenip geri konur.
function remove(instDir, keepSaves, log) {
  if (!instDir || !fs.existsSync(instDir)) return;
  if (!isInstance(instDir) && !fs.existsSync(path.join(instDir, 'HikayeTurkRP.asi'))) throw new Error('Bu klasör bir mod oyun kopyası gibi görünmüyor; silinmedi.');
  let savesBak = '';
  const saves = path.join(instDir, 'HikayeTurkRP', 'saves');
  if (keepSaves && fs.existsSync(saves)) {
    savesBak = fs.mkdtempSync(path.join(os.tmpdir(), 'hikaye_saves_'));
    fs.cpSync(saves, savesBak, { recursive: true });
  }
  for (const e of fs.readdirSync(instDir, { withFileTypes: true })) {
    const p = path.join(instDir, e.name);
    if (isLink(p)) removeLink(p);                    // önce bağlar: hedefe girmeden
    else if (e.isDirectory()) fs.rmSync(p, { recursive: true, force: true });
    else fs.unlinkSync(p);
  }
  try { fs.rmdirSync(instDir); } catch (x) { /* boş değilse kalsın */ }
  if (savesBak) {
    fs.mkdirSync(path.dirname(saves), { recursive: true });
    fs.cpSync(savesBak, saves, { recursive: true });
    fs.rmSync(savesBak, { recursive: true, force: true });
    log('Kayıtların korundu: ' + saves);
  }
}

function state(origDir, instDir, lay) {
  const s = { instDir, exists: isInstance(instDir), upToDate: false, vendor: {}, hasAsi: false };
  for (const f of VENDOR_FILES) s.vendor[f] = fs.existsSync(path.join(vendorDir(lay), f));
  if (!s.exists) return s;
  s.hasAsi = fs.existsSync(path.join(instDir, 'HikayeTurkRP.asi'));
  const launcherOk = !fs.existsSync(path.join(origDir || '', 'PlayGTAV.exe')) || fs.existsSync(path.join(instDir, 'PlayGTAV.exe'));  // eski kopyada eksik olabilir
  let ok = launcherOk && fs.existsSync(path.join(instDir, 'commandline.txt')) && s.hasAsi && fs.existsSync(lay.asi) && sameCopy(lay.asi, path.join(instDir, 'HikayeTurkRP.asi')) === true;
  if (ok && origDir) {
    // orijinal güncellendiyse (GTA5.exe farklı / bağ eski) yenilenmeli
    ok = sameCopy(path.join(origDir, 'GTA5.exe'), path.join(instDir, 'GTA5.exe'));
  }
  s.upToDate = ok;
  return s;
}

function importVendor(lay, files, log) {
  const vd = vendorDir(lay);
  fs.mkdirSync(vd, { recursive: true });
  const added = [];
  for (const f of files) {
    const base = path.basename(f);
    const canon = VENDOR_FILES.find((v) => v.toLowerCase() === base.toLowerCase());
    if (!canon) { log('Atlandı (tanınmayan dosya): ' + base); continue; }
    fs.copyFileSync(f, path.join(vd, canon));
    added.push(canon);
  }
  if (added.length) log('Eklendi: ' + added.join(', '));
  return added;
}

module.exports = { DEFAULT_ARGS, cfg, defaultInstanceDir, vendorDir, isInstance, sync, remove, state, importVendor, isLink, sameFile, VENDOR_FILES };
