'use strict';
// Başlatıcı testleri: SAHTE orijinal oyun klasöründe çalışır. Ana ilke: ORİJİNAL GTA V klasörüne hiçbir şey yazılmaz/silinmez.
// Çalıştırma: node test.js
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const http = require('http');
const crypto = require('crypto');
const { execFileSync } = require('child_process');
const core = require('./core');
const inst = require('./instance');
core.setGameRunningProbe(() => false);

let passed = 0;
function t(name, fn) {
  return Promise.resolve().then(fn).then(() => { passed++; console.log('  ok  ' + name); }, (e) => { console.error('  HATA ' + name + ': ' + e.message); process.exitCode = 1; });
}

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'hikaye_launcher_test_'));
const orig = path.join(tmp, 'GTAV');        // orijinal (salt okunur olmalı)
const instd = path.join(tmp, 'kopya');      // mod oyun kopyası
const root = path.join(tmp, 'dist');
inst.cfg.linkMinBytes = 1000;               // testte "büyük" eşiği küçük

function put(p, content) { fs.mkdirSync(path.dirname(p), { recursive: true }); fs.writeFileSync(p, content); }
put(path.join(orig, 'GTA5.exe'), 'exe-orijinal');
put(path.join(orig, 'PlayGTAV.exe'), 'launcher');
put(path.join(orig, 'bink2w64.dll'), 'dll');
put(path.join(orig, 'dinput8.dll'), 'ESKI-orijinalde-kalmis');          // üçüncü taraf: kopyaya buradan ALINMAZ
put(path.join(orig, 'x64a.rpf'), 'R'.repeat(5000));
put(path.join(orig, 'common.rpf'), 'C'.repeat(3000));
put(path.join(orig, 'version.txt'), '1.0');
put(path.join(orig, 'GTA5_BE.exe'), 'be');
put(path.join(orig, 'BattlEye', 'BEClient.dll'), 'be');
put(path.join(orig, 'Redistributables', 'vc.exe'), 'vc');
put(path.join(orig, 'update', 'update.rpf'), 'U'.repeat(4000));
put(path.join(orig, 'x64', 'audio.rpf'), 'A'.repeat(4000));
put(path.join(orig, 'GTA5.log'), 'log');
put(path.join(orig, 'Menyoo.asi'), 'menyoo');
// paket
put(path.join(root, 'mod', 'HikayeTurkRP.asi'), 'asi-v1');
put(path.join(root, 'mod', 'cef', 'libcef.dll'), 'cef-runtime');
put(path.join(root, 'mod', 'cef', 'HikayeTurkRPBrowserHost.exe'), 'cef-browser-host');
put(path.join(root, 'mod', 'cef', 'HikayeTurkRPSubprocess.exe'), 'cef-subprocess');
put(path.join(root, 'mod', 'cef', 'Resources', 'icudtl.dat'), 'cef-icu-data');
put(path.join(root, 'mod', 'cef', 'Resources', 'locales', 'en-US.pak'), 'cef-locale');
const gf = path.join(root, 'gamefiles', 'HikayeTurkRP');
put(path.join(gf, 'data', 'items.json'), '{}');
put(path.join(gf, 'addons', 'README.md'), 'oku');
put(path.join(gf, 'addons', '_ornek', 'addon.json'), '{}');
put(path.join(gf, 'addons', 'paketli', 'addon.json'), '{"v":1}');
put(path.join(root, 'version.json'), '{"version":"0.1.0"}');
put(path.join(root, 'vendor', 'ScriptHookV.dll'), 'shv');
put(path.join(root, 'vendor', 'dinput8.dll'), 'dinput-vendor');
fs.mkdirSync(path.join(root, 'companion'), { recursive: true });
const lay = core.layout(root);
const log = () => {};
const read = (p) => fs.readFileSync(p, 'utf8');

// Orijinalin parmak izi (ad + boyut + mtime + içerik özeti)
function fingerprint(dir) {
  const out = [];
  const walk = (d) => {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
      const p = path.join(d, e.name);
      if (e.isDirectory()) walk(p);
      else { const s = fs.statSync(p); out.push(path.relative(dir, p) + '|' + s.size + '|' + Math.round(s.mtimeMs) + '|' + crypto.createHash('md5').update(fs.readFileSync(p)).digest('hex')); }
    }
  };
  walk(dir);
  return out.sort().join('\n');
}
let origPrint = fingerprint(orig);

(async () => {
  await t('sürüm karşılaştırma', () => {
    assert.strictEqual(core.compareVersions('1.2.10', '1.2.9'), 1);
    assert.strictEqual(core.compareVersions('v0.1.0', '0.1.0'), 0);
    assert.strictEqual(core.compareVersions('0.9', '0.10'), -1);
  });
  await t('depo adı doğrulama', () => {
    assert(core.validRepo('kullanici/depo'));
    assert(!core.validRepo('../x'));
    assert(!core.validRepo('a/b/c'));
  });
  await t('oyun klasörü algılama', () => {
    assert(core.isGameDir(orig));
    assert(!core.isGameDir(tmp));
  });
  await t('kopya orijinalin içine konamaz', () => {
    assert.throws(() => inst.sync(orig, path.join(orig, 'icinde'), lay, log), /İÇİNDE/);
    assert(!fs.existsSync(path.join(orig, 'icinde')));
  });
  await t('kopya kuruldu: yalın (BattlEye/Redist/log yok; PlayGTAV.exe var), büyükler bağlı, küçükler kopya', () => {
    const r = inst.sync(orig, instd, lay, log);
    const names = fs.readdirSync(instd);
    for (const gerekli of ['GTA5.exe', 'PlayGTAV.exe', 'bink2w64.dll', 'x64a.rpf', 'common.rpf', 'update', 'x64', 'version.txt', 'steam_appid.txt', 'HikayeTurkRP.asi', 'HikayeTurkRP', 'ScriptHookV.dll', 'dinput8.dll']) assert(names.includes(gerekli), 'eksik: ' + gerekli);
    assert.strictEqual(read(path.join(instd, 'HikayeTurkRP', 'cef', 'libcef.dll')), 'cef-runtime');
    assert.strictEqual(read(path.join(instd, 'HikayeTurkRP', 'cef', 'HikayeTurkRPBrowserHost.exe')), 'cef-browser-host');
    assert.strictEqual(read(path.join(instd, 'HikayeTurkRP', 'cef', 'HikayeTurkRPSubprocess.exe')), 'cef-subprocess');
    assert.strictEqual(read(path.join(instd, 'HikayeTurkRP', 'cef', 'Resources', 'icudtl.dat')), 'cef-icu-data');
    assert.strictEqual(read(path.join(instd, 'HikayeTurkRP', 'cef', 'Resources', 'locales', 'en-US.pak')), 'cef-locale');
    for (const olmamali of ['BattlEye', 'Redistributables', 'GTA5_BE.exe', 'GTA5.log', 'Menyoo.asi']) assert(!names.includes(olmamali), 'kalabalık: ' + olmamali);
    assert(['symlink', 'hardlink'].includes(r.method), 'büyük dosya bağlanmadı: ' + r.method);
    assert(inst.sameFile(path.join(orig, 'x64a.rpf'), path.join(instd, 'x64a.rpf')) || inst.isLink(path.join(instd, 'x64a.rpf')));
    assert(fs.existsSync(path.join(instd, 'update', 'update.rpf')));
    assert.strictEqual(read(path.join(instd, 'dinput8.dll')), 'dinput-vendor');
    assert.strictEqual(read(path.join(instd, 'steam_appid.txt')), '271590');
    assert(/-nobattleye/.test(read(path.join(instd, 'commandline.txt'))) && /-scOfflineOnly/.test(read(path.join(instd, 'commandline.txt'))));
    assert(!fs.existsSync(path.join(orig, 'commandline.txt')));                    // orijinale yazılmadı
  });
  await t('ORİJİNAL GTA V klasörü hiç değişmedi', () => {
    assert.strictEqual(fingerprint(orig), origPrint);
    assert(!fs.existsSync(path.join(orig, 'HikayeTurkRP.asi')) && !fs.existsSync(path.join(orig, 'HikayeTurkRP')) && !fs.existsSync(path.join(orig, 'steam_appid.txt')));
  });
  await t('tekrar kurulum idempotent; kayıtlar ve kullanıcı eklentileri korunur', () => {
    put(path.join(instd, 'HikayeTurkRP', 'saves', 'karma_0.json'), 'kayit');
    put(path.join(instd, 'HikayeTurkRP', 'addons', 'paketli', 'addon.json'), '{"v":"kullanici"}');
    put(path.join(instd, 'HikayeTurkRP', 'addons', 'benimki', 'addon.json'), '{}');
    inst.sync(orig, instd, lay, log);
    assert.strictEqual(read(path.join(instd, 'HikayeTurkRP', 'saves', 'karma_0.json')), 'kayit');
    assert.strictEqual(read(path.join(instd, 'HikayeTurkRP', 'addons', 'paketli', 'addon.json')), '{"v":"kullanici"}');
    assert(fs.existsSync(path.join(instd, 'HikayeTurkRP', 'addons', 'benimki', 'addon.json')));
    assert(fs.existsSync(path.join(instd, 'HikayeTurkRP', 'addons', '_ornek', 'addon.json')));
    assert.strictEqual(fingerprint(orig), origPrint);
  });
  await t('durum: orijinal exe değişince kopya "güncel değil", sync yenileyince güncel', () => {
    assert(inst.state(orig, instd, lay).upToDate);
    fs.writeFileSync(path.join(orig, 'GTA5.exe'), 'exe-yeni-surum');   // Steam güncellemesini taklit et (testte orijinali biz değiştiriyoruz)
    origPrint = fingerprint(orig);
    assert(!inst.state(orig, instd, lay).upToDate);
    inst.sync(orig, instd, lay, log);
    assert.strictEqual(read(path.join(instd, 'GTA5.exe')), 'exe-yeni-surum');
    assert(inst.state(orig, instd, lay).upToDate);
    assert.strictEqual(fingerprint(orig), origPrint);
  });
  await t('özel başlatma parametresi commandline.txt\'e yazılır', () => {
    inst.sync(orig, instd, lay, log, { args: '-foo -bar' });
    assert.strictEqual(read(path.join(instd, 'commandline.txt')).trim(), '-foo -bar');
    inst.sync(orig, instd, lay, log);
    assert(/-nobattleye/.test(read(path.join(instd, 'commandline.txt'))));
  });
  // Türkçe yama = fark yaması (xdelta3): Rockstar dosyası dağıtılmaz, kullanıcının kendi orijinalinden üretilir
  const xdReal = path.join(__dirname, '..', 'tools', 'bin', 'xdelta3.exe');
  await t('Türkçe yama (fark): orijinalden üretilir, SHA-256 doğrulanır, aç/kapat, üzerine yazmaz', async () => {
    if (!fs.existsSync(xdReal)) { console.log('      (xdelta3.exe yok: atlandı)'); return; }
    const crypto2 = require('crypto');
    const sha = (p) => crypto2.createHash('sha256').update(fs.readFileSync(p)).digest('hex').toUpperCase();
    // sahte "orijinal" update.rpf ve yamalı sürümü (araya bayt eklenmiş + bir bölüm değişmiş)
    const base = crypto2.randomBytes(300000);
    const patched = Buffer.concat([base.subarray(0, 100000), Buffer.from('TURKCE-YAMA-METNI'.repeat(500)), base.subarray(100000)]);
    patched.fill(0x41, 250000, 250100);
    put(path.join(orig, 'update', 'update.rpf'), base);
    origPrint = fingerprint(orig);
    put(path.join(tmp, 'yamali', 'update.rpf'), patched);
    fs.mkdirSync(path.join(root, 'tr-yama'), { recursive: true });
    fs.mkdirSync(path.join(root, 'bin'), { recursive: true });
    fs.copyFileSync(xdReal, path.join(root, 'bin', 'xdelta3.exe'));
    execFileSync(xdReal, ['-e', '-9', '-f', '-s', path.join(orig, 'update', 'update.rpf'), path.join(tmp, 'yamali', 'update.rpf'), path.join(root, 'tr-yama', 'update.xd3')], { windowsHide: true });
    core.writeJson(path.join(root, 'tr-yama', 'manifest.json'), { game_version: 't', files: [{ name: 'update.rpf', delta: 'update.xd3', src_size: base.length, out_size: patched.length, out_sha256: sha(path.join(tmp, 'yamali', 'update.rpf')) }] });
    assert(core.trPatchSource(lay));
    assert(!core.patchState(instd).present || (fs.rmSync(path.join(instd, 'mods'), { recursive: true, force: true }), true));
    inst.sync(orig, instd, lay, log);
    const w = await core.installTrPatch(instd, orig, lay, log);
    assert(w.length === 1 && /OpenIV/.test(w[0]));                       // OpenIV.asi yok -> uyarı
    assert.strictEqual(sha(path.join(instd, 'mods', 'update', 'update.rpf')), sha(path.join(tmp, 'yamali', 'update.rpf')));
    assert(!fs.existsSync(path.join(orig, 'mods')));                      // orijinalde mods YOK
    assert.strictEqual(fingerprint(orig), origPrint);                     // orijinal değişmedi
    assert(!fs.existsSync(path.join(instd, 'mods.hazirlaniyor')));
    await assert.rejects(() => core.installTrPatch(instd, orig, lay, log), /zaten/);
    let p = core.togglePatch(instd, false);
    assert(!p.enabled && fs.existsSync(path.join(instd, 'mods.kapali', 'update', 'update.rpf')));
    p = core.togglePatch(instd, true);
    assert(p.enabled && p.turkish);
  });
  await t('Türkçe yama: sürüm uyuşmazsa üretmez, yarım klasör bırakmaz', async () => {
    if (!fs.existsSync(xdReal)) { console.log('      (xdelta3.exe yok: atlandı)'); return; }
    fs.rmSync(path.join(instd, 'mods'), { recursive: true, force: true });
    const man = core.readJson(path.join(root, 'tr-yama', 'manifest.json'));
    man.files[0].src_size += 7;                                            // oyun güncellenmiş gibi
    core.writeJson(path.join(root, 'tr-yama', 'manifest.json'), man);
    await assert.rejects(() => core.installTrPatch(instd, orig, lay, log), /sürümü yamayla uyuşmuyor/);
    assert(!fs.existsSync(path.join(instd, 'mods')) && !fs.existsSync(path.join(instd, 'mods.hazirlaniyor')));
    man.files[0].src_size -= 7;
    man.files[0].out_sha256 = '0'.repeat(64);                              // bozuk çıktı
    core.writeJson(path.join(root, 'tr-yama', 'manifest.json'), man);
    await assert.rejects(() => core.installTrPatch(instd, orig, lay, log), /doğrulanamadı/);
    assert(!fs.existsSync(path.join(instd, 'mods')) && !fs.existsSync(path.join(instd, 'mods.hazirlaniyor')));
  });
  await t('vendor içe aktarma: yalnız tanınan dosyalar', () => {
    const f1 = path.join(tmp, 'OpenIV.asi'), f2 = path.join(tmp, 'rastgele.txt');
    put(f1, 'oiv'); put(f2, 'x');
    assert.deepStrictEqual(inst.importVendor(lay, [f1, f2], log), ['OpenIV.asi']);
    assert(!fs.existsSync(path.join(root, 'vendor', 'rastgele.txt')));
    inst.sync(orig, instd, lay, log);
    assert.strictEqual(read(path.join(instd, 'OpenIV.asi')), 'oiv');
  });
  await t('kopyayı sil: bağlar yalnız bağ olarak kalkar, ORİJİNAL içerik korunur, kayıtlar geri', () => {
    inst.remove(instd, true, log);
    assert.strictEqual(fingerprint(orig), origPrint, 'orijinal dosyalar değişti!');
    assert(fs.existsSync(path.join(orig, 'update', 'update.rpf')) && fs.existsSync(path.join(orig, 'x64a.rpf')));
    assert(!fs.existsSync(path.join(instd, 'GTA5.exe')) && !fs.existsSync(path.join(instd, 'x64a.rpf')) && !fs.existsSync(path.join(instd, 'update')));
    assert.strictEqual(read(path.join(instd, 'HikayeTurkRP', 'saves', 'karma_0.json')), 'kayit');
  });
  await t('yanlış klasörü silmeyi reddeder', () => {
    const x = path.join(tmp, 'baska'); put(path.join(x, 'dosya.txt'), 'x');
    assert.throws(() => inst.remove(x, false, log), /gibi görünmüyor/);
    assert(fs.existsSync(path.join(x, 'dosya.txt')));
  });
  await t('güncelleme uygulama (zip): launcher/ ve ayarlar korunur', () => {
    const upd = path.join(tmp, 'upd', 'HikayeTurkRP');
    put(path.join(upd, 'mod', 'HikayeTurkRP.asi'), 'asi-v2');
    put(path.join(upd, 'launcher', 'dokunma.txt'), 'x');
    put(path.join(upd, 'version.json'), '{"version":"0.2.0"}');
    put(path.join(root, 'launcher-settings.json'), '{"gameDir":"x"}');
    const zip = path.join(tmp, 'upd.zip');
    execFileSync('powershell', ['-NoProfile', '-Command', 'Compress-Archive -Path $env:HK_SRC -DestinationPath $env:HK_ZIP -Force'],
      { windowsHide: true, env: Object.assign({}, process.env, { HK_SRC: upd, HK_ZIP: zip }) });
    assert.strictEqual(core.applyUpdate(zip, root, log), '0.2.0');
    assert.strictEqual(read(path.join(root, 'mod', 'HikayeTurkRP.asi')), 'asi-v2');
    assert(!fs.existsSync(path.join(root, 'launcher', 'dokunma.txt')));
    assert.strictEqual(read(path.join(root, 'launcher-settings.json')), '{"gameDir":"x"}');
  });
  await t('yazılamaz kurulum klasörü: paket çalışma klasörüne alınır; yeni olan çalışma klasörü korunur', () => {
    const bundle = path.join(tmp, 'ProgramFiles', 'HikayeRP');
    const work = path.join(tmp, 'LocalAppData', 'Pkg');
    put(path.join(bundle, 'mod', 'HikayeTurkRP.asi'), 'asi-b1');
    put(path.join(bundle, 'companion', 'server.py'), 'srv');
    put(path.join(bundle, 'gamefiles', 'HikayeTurkRP', 'data', 'x.json'), '{}');
    put(path.join(bundle, 'version.json'), '{"version":"0.1.0"}');
    assert(core.syncBundle(bundle, work, log));
    assert.strictEqual(read(path.join(work, 'mod', 'HikayeTurkRP.asi')), 'asi-b1');
    put(path.join(work, 'vendor', 'ScriptHookV.dll'), 'shv');                 // kullanıcı verisi
    assert(!core.syncBundle(bundle, work, log));                              // aynı sürüm: dokunma
    put(path.join(work, 'version.json'), '{"version":"0.3.0"}');             // GitHub güncellemesi daha yeni
    put(path.join(work, 'mod', 'HikayeTurkRP.asi'), 'asi-guncel');
    assert(!core.syncBundle(bundle, work, log));
    assert.strictEqual(read(path.join(work, 'mod', 'HikayeTurkRP.asi')), 'asi-guncel');
    put(path.join(bundle, 'version.json'), '{"version":"0.5.0"}');           // yeni kurulum programı
    put(path.join(bundle, 'mod', 'HikayeTurkRP.asi'), 'asi-b5');
    assert(core.syncBundle(bundle, work, log));
    assert.strictEqual(read(path.join(work, 'mod', 'HikayeTurkRP.asi')), 'asi-b5');
    assert.strictEqual(read(path.join(work, 'vendor', 'ScriptHookV.dll')), 'shv');  // vendor korunur
  });
  await t('yazılabilirlik denetimi', () => {
    assert(core.isWritable(path.join(tmp, 'yeni_klasor')));
    put(path.join(tmp, 'dosya.txt'), 'x');
    assert(!core.isWritable(path.join(tmp, 'dosya.txt', 'alt')));          // dosyanın altına klasör açılamaz: yazılamaz say
  });
  await t('port algılama', async () => {
    const srv = http.createServer(() => {}).listen(0, '127.0.0.1');
    await new Promise((r) => srv.once('listening', r));
    assert(await core.portOpen(srv.address().port));
    srv.close();
    assert(!(await core.portOpen(1)));
  });
  await t('python sunucusu: başlat, hazır bekle, ağaçla kapat', async () => {
    if (!core.checkPython()) { console.log('      (taşınabilir python yok: atlandı)'); return; }
    const comp = path.join(tmp, 'dist2', 'companion');
    fs.mkdirSync(comp, { recursive: true });
    const port = 18765;
    fs.writeFileSync(path.join(comp, 'server.py'), `import socket,time\ns=socket.socket();s.bind(("127.0.0.1",${port}));s.listen()\nprint("hazir",flush=True)\ntime.sleep(60)\n`);
    const out = [];
    const child = core.startServer({ companionDir: comp }, 'python', (l) => out.push(l));
    assert(await core.waitPort(port, 15000, () => child.alive), 'sunucu portu açılmadı');
    core.killTree(child);
    await new Promise((r) => setTimeout(r, 1200));
    assert(!(await core.portOpen(port)), 'sunucu kapanmadı');
  });
  await t('GitHub: depo ayarsızsa reddedilir', async () => {
    const r = await core.checkUpdate(lay, { githubRepo: '' }, '');
    assert(!r.ok && /ayarlanmad/.test(r.reason));
  });
  // ---- eklenti zip'i ----
  const zipDir = (src, zip) => execFileSync('powershell', ['-NoProfile', '-Command', 'Compress-Archive -Path (Join-Path $env:HK_SRC "*") -DestinationPath $env:HK_ZIP -Force'],
    { windowsHide: true, env: Object.assign({}, process.env, { HK_SRC: src, HK_ZIP: zip }) });
  const addonRoot = path.join(tmp, 'addon_test_root');
  fs.mkdirSync(addonRoot, { recursive: true });
  const alay = core.layout(addonRoot);
  const mk = (name, files) => {
    const d = path.join(tmp, 'azip_' + name);
    for (const [rel, c] of Object.entries(files)) put(path.join(d, rel), c);
    const z = path.join(tmp, name + '.zip');
    zipDir(d, z);
    return z;
  };
  const goodMission = JSON.stringify({ id: 'x_gorev', giver: 'a', npcs: { a: { model: 'm', pos: [0, 0, 0] } }, stages: [{ id: 's1', type: 'talk', npc: 'a' }, { id: 's2', type: 'interact' }] });
  await t('eklenti zip: yüklenir, user-addons ve oyun kopyasına konur; görev uyarıları verilir', () => {
    const z = mk('iyi', { 'addon.json': '{"id":"deneme_paket","name":"Deneme","version":"1.2"}', 'missions/g1.json': goodMission, 'web/siteler.com/index.hml': '<page title="x"></page>' });
    fs.mkdirSync(path.join(instd, 'HikayeTurkRP'), { recursive: true });
    const r = core.installAddonZip(z, alay, instd, () => {});
    assert.strictEqual(r.id, 'deneme_paket');
    assert(fs.existsSync(path.join(alay.root, 'user-addons', 'deneme_paket', 'missions', 'g1.json')));
    assert(fs.existsSync(path.join(instd, 'HikayeTurkRP', 'addons', 'deneme_paket', 'addon.json')));
    assert(r.warnings.some((w) => /pos yok: s2/.test(w)), 'görev uyarısı beklenir');
    assert.strictEqual(core.listAddons(alay)[0].name, 'Deneme');
  });
  await t('eklenti zip: üst klasörlü paket ve güncelleme', () => {
    const z = mk('ustklasor', { 'paketim/addon.json': '{"id":"deneme_paket","version":"2.0"}', 'paketim/items.json': '{"items":{}}' });
    const r = core.installAddonZip(z, alay, instd, () => {});
    assert(r.replaced);
    assert.strictEqual(core.listAddons(alay)[0].version, '2.0');
  });
  await t('eklenti zip: exe/dll/betik içeren paket reddedilir', () => {
    const z = mk('kotu_exe', { 'addon.json': '{"id":"kotu_paket"}', 'kotu.exe': 'MZ' });
    assert.throws(() => core.installAddonZip(z, alay, instd, () => {}), /reddedildi/);
    assert(!fs.existsSync(path.join(alay.root, 'user-addons', 'kotu_paket')));
  });
  await t('eklenti zip: addon.json yok / bozuk JSON / geçersiz kimlik reddedilir', () => {
    assert.throws(() => core.installAddonZip(mk('yok', { 'items.json': '{}' }), alay, instd, () => {}), /addon\.json bulunamad/);
    assert.throws(() => core.installAddonZip(mk('bozuk', { 'addon.json': '{"id":"b1"}', 'items.json': '{bozuk' }), alay, instd, () => {}), /JSON bozuk/);
    assert.throws(() => core.installAddonZip(mk('idk', { 'addon.json': '{"id":"_sablon"}' }), alay, instd, () => {}), /id/);
  });
  await t('eklenti: kopya hazırlanınca user-addons kopyaya konur; kaldırınca silinir', () => {
    core.installAddonZip(mk('iyi2', { 'addon.json': '{"id":"kalici","name":"Kalıcı"}' }), alay, '', () => {});
    assert(fs.existsSync(path.join(alay.root, 'user-addons', 'kalici', 'addon.json')));
    core.removeAddon('kalici', alay, instd, () => {});
    assert(!fs.existsSync(path.join(alay.root, 'user-addons', 'kalici')));
    assert.throws(() => core.removeAddon('../etc', alay, instd, () => {}), /Geçersiz/);
  });
  fs.rmSync(tmp, { recursive: true, force: true });
  console.log(passed + ' test geçti' + (process.exitCode ? ', HATA VAR' : ''));
})();
