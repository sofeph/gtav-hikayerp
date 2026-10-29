'use strict';
const $ = (id) => document.getElementById(id);
let current = null;      // son durum
let lastUpdate = null;

// ---------- sekmeler ----------
function showTab(name) {
  for (const t of document.querySelectorAll('.tab')) {
    const on = t.dataset.tab === name;
    t.classList.toggle('active', on);
    t.setAttribute('aria-selected', on ? 'true' : 'false');
  }
  for (const id of ['play', 'settings', 'log']) $('tab-' + id).classList.toggle('hidden', id !== name);
}
document.querySelectorAll('.tab').forEach((t) => (t.onclick = () => showTab(t.dataset.tab)));

// ---------- yardımcılar ----------
function appendLog(line) {
  const el = $('log');
  el.textContent += line + '\n';
  el.scrollTop = el.scrollHeight;
}
async function run(btn, fn) {
  btn.disabled = true;
  try {
    const r = await fn();
    if (r && r.ok === false && r.error) { appendLog('✖ ' + r.error); toast(r.error, true); }
    return r;
  } finally { btn.disabled = false; }
}
function toast(text, bad) {
  banner('<div class="grow">' + String(text).replace(/</g, '&lt;') + '</div>', !!bad);
  setTimeout(() => { if ($('banner').textContent.includes(String(text).slice(0, 20))) $('banner').classList.add('hidden'); }, 6000);
}
function banner(html, warn) {
  const b = $('banner');
  b.className = 'banner' + (warn ? ' warn' : '');
  b.innerHTML = html;
}

// ---------- Oyna sekmesi: hazırlık listesi ----------
function item(ico, title, sub, btn) {
  const li = document.createElement('li');
  li.innerHTML = '<span class="ico ' + ico + '">' + (ico === 'ok' ? '✓' : ico === 'bad' ? '!' : ico === 'warn' ? '!' : '·') + '</span>' +
    '<span class="txt"><b></b><small></small></span>';
  li.querySelector('b').textContent = title;
  li.querySelector('small').textContent = sub || '';
  if (btn) {
    const b = document.createElement('button');
    b.className = 'ghost small';
    b.textContent = btn.label;
    b.onclick = btn.fn;
    li.appendChild(b);
  }
  return li;
}

function render(st) {
  current = st;
  const v = (st.inst && st.inst.vendor) || {};
  const shvMissing = ['ScriptHookV.dll', 'dinput8.dll'].filter((f) => !v[f]);
  const p = st.patch || {};
  const items = [];
  let blocking = 0, warn = 0;

  // 1) GTA V
  if (st.gameOk) items.push(item('ok', 'GTA V bulundu', st.gameDir));
  else { blocking++; items.push(item('bad', 'GTA V klasörü bulunamadı', 'Orijinal oyun klasörünü seçmen gerekiyor', { label: 'Seç…', fn: pickGame })); }

  // 2) Yapay zekâ / Ses ortamı (Taşınabilir Python)
  if (st.python) {
    items.push(item('ok', 'Yapay zekâ ortamı hazır', st.python));
  } else {
    items.push(item('warn', 'Yapay zekâ ortamı', 'Başlatınca otomatik kurulur (sisteme Python kurmanız gerekmez)', { label: 'Şimdi Hazırla', fn: autoSetup }));
  }

  // 3) ScriptHookV
  if (!shvMissing.length) items.push(item('ok', 'ScriptHookV hazır', v['OpenIV.asi'] ? 'OpenIV de ekli' : 'OpenIV.asi yok (yalnızca Türkçe yama için gerekli)'));
  else {
    blocking++;
    items.push(item('bad', 'ScriptHookV eksik', 'Eksik: ' + shvMissing.join(', ') + ' · modun çalışması için bir kez eklenir', { label: 'Ekle…', fn: addVendor }));
  }

  // 4) Oyun kopyası
  if (st.upToDate) items.push(item('ok', 'Oyun kopyası hazır', st.inst.instDir));
  else if (st.installed) { warn++; items.push(item('warn', 'Oyun kopyası güncellenecek', 'Başlat\'a basınca otomatik yenilenir')); }
  else items.push(item('', 'Oyun kopyası henüz yok', 'Başlat\'a basınca otomatik hazırlanır (disk yeri harcamaz)'));

  // 5) Türkçe yama (isteğe bağlı)
  if (p.present) {
    items.push(item(p.enabled ? 'ok' : 'warn', 'Türkçe yama ' + (p.enabled ? 'açık' : 'kapalı'), (p.sizeMB || 0) + ' MB' + (v['OpenIV.asi'] ? '' : ' · OpenIV.asi gerekli'),
      { label: p.enabled ? 'Kapat' : 'Aç', fn: (e) => run(e.target, () => window.api.togglePatch(!p.enabled)) }));
  } else if (p.source) {
    items.push(item('', 'Türkçe yama', 'Orijinal dosyalardan üretilir (Başlat\'ta otomatik)', { label: 'Kur', fn: (e) => run(e.target, () => window.api.installTrPatch()) }));
  }

  // 6) Çalışan şeyler
  items.push(item(st.serverRunning ? 'ok' : '', 'Yapay zekâ sunucusu', st.serverRunning ? 'çalışıyor' : 'kapalı'));
  items.push(item(st.gameRunning ? 'ok' : '', 'GTA V', st.gameRunning ? 'çalışıyor' : 'kapalı'));

  const ul = $('checklist');
  ul.replaceChildren(...items);

  // büyük kart
  const hero = document.querySelector('.hero');
  hero.classList.remove('ok', 'warn');
  if (st.gameRunning && st.serverRunning) {
    $('heroTitle').textContent = 'Oyun çalışıyor';
    $('heroSub').textContent = 'İyi eğlenceler! Oyun kapanınca sunucu da otomatik kapanır.';
    hero.classList.add('ok');
  } else if (blocking) {
    $('heroTitle').textContent = blocking === 1 ? 'Bir şey eksik' : blocking + ' şey eksik';
    $('heroSub').textContent = 'Aşağıdaki kırmızı maddeleri tamamla, sonra başlat.';
    hero.classList.add('warn');
  } else {
    $('heroTitle').textContent = 'Hazırsın';
    $('heroSub').textContent = warn ? 'Başlat\'a basınca kopya otomatik yenilenir.' : 'Başlat\'a bas: sunucu açılır ve oyun doğrudan Hikaye RP\'de başlar.';
    hero.classList.add('ok');
  }
  $('btnAuto').disabled = !!st.gameRunning;
  $('btnStart').disabled = blocking > 0 || (!!st.gameRunning && !!st.serverRunning);
  $('btnStart').textContent = st.gameRunning ? 'Çalışıyor…' : 'Oyunu Başlat';
  $('btnStop').disabled = !st.serverRunning;

  // Ayarlar sekmesindeki düğmeler
  $('btnPatch').disabled = !p.present;
  $('btnTrInstall').disabled = !!p.present || !p.source;
  $('btnTrInstall').title = p.source ? '' : 'Pakette tr-yama/mods yok';
  $('btnPatch').dataset.enabled = p.enabled ? '1' : '';
}

async function pickGame() {
  const r = await window.api.pickGameDir();
  if (r.ok) { $('gameDir').value = r.dir; toast('GTA V klasörü ayarlandı.'); }
  else if (r.error) { appendLog('✖ ' + r.error); toast(r.error, true); }
}
async function addVendor() {
  const r = await window.api.importVendor();
  if (r && r.ok) {
    toast((r.auto ? 'Otomatik bulundu ve eklendi: ' : 'Eklendi: ') + r.added.join(', ') + (r.missing && r.missing.length ? ' · Hâlâ eksik: ' + r.missing.join(', ') : ''));
  } else if (r && r.auto === undefined && r.ok === false && !r.added) toast('Bilgisayarda ScriptHookV/OpenIV bulunamadı. ScriptHookV zip\'ini İndirilenler\'e koy ya da dosyaları seç.', true);
  else if (r && r.added && !r.added.length) toast('Tanınan dosya seçilmedi (ScriptHookV.dll, dinput8.dll, OpenIV.asi).', true);
}

// ---------- güncelleme ----------
function showUpdate(r) {
  lastUpdate = r;
  if (!r || !r.ok) { if (r && r.reason && /ayarlanmad/.test(r.reason) === false) banner('Güncelleme denetimi: ' + r.reason, true); return; }
  if (!r.available) { banner('<div class="grow">Güncelsin: ' + r.current + ' sürümü en yeni.</div>'); setTimeout(() => $('banner').classList.add('hidden'), 4000); return; }
  const notes = (r.notes || '').split('\n').slice(0, 4).join(' · ').slice(0, 220);
  banner('<div class="grow"><b>Yeni sürüm: ' + r.latest + '</b> (şu an ' + r.current + ')<br><small>' + notes.replace(/</g, '&lt;') +
    '</small></div><button id="btnApply" class="primary">' + (r.asset ? 'İndir ve güncelle' : 'Sayfayı aç') + '</button>');
  $('btnApply').onclick = async () => {
    if (!r.asset) return window.api.openUrl(r.page);
    $('btnApply').disabled = true;
    const res = await window.api.applyUpdate(r);
    if (res.ok) { banner('<div class="grow">Güncelleme tamam (' + res.version + '). Başlatıcıyı yeniden başlat.</div><button id="btnRe" class="primary">Yeniden başlat</button>'); $('btnRe').onclick = () => window.api.relaunch(); }
    else banner('Güncelleme başarısız: ' + (res.error || ''), true);
  };
}

// ---------- ayarlar ----------
function fillSettings(s) {
  $('gameDir').value = s.gameDir || '';
  $('instanceDir').value = s.instanceDir || '';
  $('gameArgs').value = s.gameArgs || '';
  $('python').value = s.python && s.python !== 'python' ? s.python : '';
  $('githubRepo').value = s.githubRepo || '';
  $('autoCheck').checked = !!s.autoCheck;
  $('trPatch').checked = s.trPatch !== false;
  $('autoMenu').checked = s.autoMenu !== false;
  $('token').placeholder = s.hasToken ? 'anahtar kayıtlı (değiştirmek için yaz, silmek için boşluk bırakıp kaydet)' : 'boş bırak: herkese açık depo';
}

window.api.onStep((t) => { const el = $('stepLine'); el.classList.remove('hidden'); el.textContent = t; });
window.api.onLog(appendLog);
window.api.onState(render);
window.api.onUpdateInfo(showUpdate);
window.api.onProgress((p) => { const el = $('prog'); el.classList.remove('hidden'); el.textContent = '%' + p; if (p >= 100) setTimeout(() => el.classList.add('hidden'), 1500); });

(async () => {
  const init = await window.api.init();
  $('ver').textContent = 'v' + init.version;
  fillSettings(init.settings);
  render(init.state);
  if (!init.state.gameOk) showTab('settings');  // ilk kullanım: önce GTA V klasörü
})();

// ---------- düğmeler ----------
$('btnStart').onclick = (e) => { showTab('play'); run(e.target, () => window.api.start()); };
$('btnAuto').onclick = (e) => run(e.target, async () => {
  const r = await window.api.autoSetup();
  $('stepLine').classList.add('hidden');
  if (r && r.ok) {
    if (r.todo && r.todo.length) banner('<div class="grow"><b>Elle yapılacaklar:</b><br><small>' + r.todo.map((x) => '• ' + String(x).replace(/</g, '&lt;')).join('<br>') + '</small></div>', true);
    else toast('Her şey hazır. Oyunu başlatabilirsin.');
  }
  return r;
});
$('btnStop').onclick = (e) => run(e.target, () => window.api.stop());
$('btnInstall').onclick = (e) => run(e.target, () => window.api.install());
$('btnUninstall').onclick = (e) => { if (confirm('Mod oyun kopyası silinsin mi?\nKayıtların korunur; orijinal GTA V\'ye dokunulmaz.')) run(e.target, () => window.api.uninstall(false)); };
$('btnUpdate').onclick = (e) => run(e.target, async () => { const r = await window.api.checkUpdate(); showUpdate(r); return r; });
$('btnTrInstall').onclick = (e) => run(e.target, () => window.api.installTrPatch());
$('btnPatch').onclick = (e) => run(e.target, () => window.api.togglePatch(!e.target.dataset.enabled));
$('btnVendor').onclick = () => addVendor();

// ---------- eklentiler ----------
function renderAddons(list) {
  const el = $('addonList');
  if (!el) return;
  el.innerHTML = '';
  if (!list || !list.length) { el.textContent = 'Yüklü eklenti yok.'; return; }
  for (const a of list) {
    const row = document.createElement('div');
    row.className = 'row';
    const label = document.createElement('span');
    label.textContent = a.name + (a.version ? ' v' + a.version : '') + '  (' + a.id + ')';
    const del = document.createElement('button');
    del.className = 'ghost';
    del.textContent = 'Kaldır';
    del.onclick = async () => {
      const r = await window.api.removeAddon(a.id);
      if (r && r.ok) { toast('Eklenti kaldırıldı: ' + a.id); renderAddons(r.addons); } else toast((r && r.error) || 'Kaldırılamadı', true);
    };
    row.append(label, del);
    el.appendChild(row);
  }
}
async function loadAddons() { const r = await window.api.listAddons(); if (r && r.ok) renderAddons(r.addons); }
$('btnAddon').onclick = async () => {
  const r = await window.api.installAddon();
  if (!r || r.canceled) return;
  if (!r.ok) { toast('Eklenti yüklenemedi: ' + r.error, true); return; }
  toast('Eklenti ' + (r.replaced ? 'güncellendi' : 'yüklendi') + ': ' + r.name + ' (' + r.files + ' dosya)' + (r.warnings && r.warnings.length ? ' · ' + r.warnings.length + ' uyarı (Kayıt sekmesi)' : '') + (r.gameRunning ? ' · oyun açık: yeni açılışta etkin olur' : ''));
  loadAddons();
};
loadAddons();
$('btnPick').onclick = () => pickGame();
$('btnSave').onclick = async () => {
  const patch = { gameDir: $('gameDir').value, instanceDir: $('instanceDir').value, gameArgs: $('gameArgs').value, python: $('python').value || 'python', githubRepo: $('githubRepo').value, autoCheck: $('autoCheck').checked, trPatch: $('trPatch').checked, autoMenu: $('autoMenu').checked };
  if ($('token').value !== '') patch.token = $('token').value.trim();
  const r = await window.api.saveSettings(patch);
  $('saveMsg').textContent = r.ok ? 'Kaydedildi ✓' : r.error;
  $('saveMsg').style.color = r.ok ? '' : 'var(--bad)';
  $('token').value = '';
  setTimeout(() => ($('saveMsg').textContent = ''), 3500);
};
document.querySelectorAll('[data-open]').forEach((b) => (b.onclick = () => window.api.open(b.dataset.open)));
