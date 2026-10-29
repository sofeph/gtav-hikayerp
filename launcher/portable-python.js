'use strict';
// Taşınabilir (portable) Python yöneticisi.
// Sistemde Python olmasa bile çalışır: Python 3.12 embeddable paketi python.org'dan
// indirilir, pip bootstrap edilir ve tüm bağımlılıklar bu yerel Python'a kurulur.
// Python 3.12 tercih edilir çünkü torch, coqui-tts, faster-whisper hepsiyle uyumludur.

const fs = require('fs');
const path = require('path');
const os = require('os');
const http = require('http');
const https = require('https');
const { execFileSync, spawn } = require('child_process');

const PY_VERSION = '3.12.7';
const PY_EMBED_URL = 'https://www.python.org/ftp/python/' + PY_VERSION + '/python-' + PY_VERSION + '-embed-amd64.zip';
const GET_PIP_URL = 'https://bootstrap.pypa.io/get-pip.py';

// %LOCALAPPDATA%\HikayeTurkRP\python — güncelleme/kopya silme etkilemez
const BASE_DIR = path.join(
  process.env.LOCALAPPDATA || path.join(os.homedir(), 'AppData', 'Local'),
  'HikayeTurkRP', 'python'
);

function exe() {
  return path.join(BASE_DIR, 'python.exe');
}

function installed() {
  try {
    return fs.existsSync(exe()) && !!execFileSync(exe(), ['--version'], { encoding: 'utf8', windowsHide: true }).trim();
  } catch (e) { return false; }
}

function pipReady() {
  if (!installed()) return false;
  try {
    execFileSync(exe(), ['-m', 'pip', '--version'], { windowsHide: true, stdio: 'ignore' });
    return true;
  } catch (e) { return false; }
}

// HTTPS GET → Buffer (yönlendirmeleri takip eder)
function httpGetBuf(url, redirects) {
  return new Promise(function (resolve, reject) {
    var u = new URL(url);
    var mod = u.protocol === 'https:' ? https : http;
    mod.get({ hostname: u.hostname, port: u.port, path: u.pathname + u.search, headers: { 'User-Agent': 'HikayeTurkRP-Launcher' } }, function (res) {
      if ([301, 302, 303, 307, 308].indexOf(res.statusCode) >= 0 && res.headers.location && (redirects || 0) < 6) {
        res.resume();
        var next = res.headers.location;
        if (!next.startsWith('http')) next = new URL(next, url).toString();
        return resolve(httpGetBuf(next, (redirects || 0) + 1));
      }
      if (res.statusCode !== 200) { res.resume(); return reject(new Error('HTTP ' + res.statusCode + ' ' + url)); }
      var chunks = [];
      res.on('data', function (c) { chunks.push(c); });
      res.on('end', function () { resolve(Buffer.concat(chunks)); });
      res.on('error', reject);
    }).on('error', reject);
  });
}

// HTTPS → dosyaya kaydet (yönlendirme + ilerleme)
function httpDownload(url, destFile, onProgress, redirects) {
  return new Promise(function (resolve, reject) {
    var u = new URL(url);
    var mod = u.protocol === 'https:' ? https : http;
    mod.get({ hostname: u.hostname, port: u.port, path: u.pathname + u.search, headers: { 'User-Agent': 'HikayeTurkRP-Launcher' } }, function (res) {
      if ([301, 302, 303, 307, 308].indexOf(res.statusCode) >= 0 && res.headers.location && (redirects || 0) < 6) {
        res.resume();
        var next = res.headers.location;
        if (!next.startsWith('http')) next = new URL(next, url).toString();
        return resolve(httpDownload(next, destFile, onProgress, (redirects || 0) + 1));
      }
      if (res.statusCode !== 200) { res.resume(); return reject(new Error('HTTP ' + res.statusCode + ' ' + url)); }
      var total = parseInt(res.headers['content-length'] || '0', 10);
      fs.mkdirSync(path.dirname(destFile), { recursive: true });
      var out = fs.createWriteStream(destFile);
      var got = 0;
      var lastPct = -10;
      res.on('data', function (c) {
        got += c.length;
        if (onProgress && total) {
          var pct = Math.round(got * 100 / total);
          if (pct - lastPct >= 5) { lastPct = pct; onProgress(pct); }
        }
      });
      res.pipe(out);
      out.on('finish', resolve);
      out.on('error', reject);
      res.on('error', reject);
    }).on('error', reject);
  });
}

// PowerShell ile zip aç
function expandZip(zip, dest) {
  fs.mkdirSync(dest, { recursive: true });
  execFileSync('powershell', ['-NoProfile', '-Command',
    'Expand-Archive -LiteralPath $env:HK_ZIP -DestinationPath $env:HK_DEST -Force'],
    { windowsHide: true, env: Object.assign({}, process.env, { HK_ZIP: zip, HK_DEST: dest }) });
}

// Python embeddable'ın ._pth dosyasını düzelt: "#import site" → "import site"
// Bu yapılmazsa pip ve site-packages ÇALIŞMAZ.
function fixPthFile() {
  var files = [];
  try { files = fs.readdirSync(BASE_DIR); } catch (e) { return; }
  for (var i = 0; i < files.length; i++) {
    if (/^python\d+\._pth$/i.test(files[i])) {
      var fp = path.join(BASE_DIR, files[i]);
      var txt = fs.readFileSync(fp, 'utf8');
      if (txt.indexOf('#import site') >= 0) {
        txt = txt.replace('#import site', 'import site');
      }
      if (txt.indexOf('Lib/site-packages') === -1) {
        txt = txt + '\nLib/site-packages\n';
      }
      fs.writeFileSync(fp, txt, 'utf8');
    }
  }
}

// Tam kurulum: indir → çıkar → pip bootstrap → hazır
// Zaten kuruluysa tekrar indirmez; pip yoksa sadece pip'i kurar.
async function setup(onLog) {
  if (installed() && pipReady()) {
    onLog('Taşınabilir Python zaten kurulu: ' + exe());
    return exe();
  }

  if (!installed()) {
    // 1) Python embeddable indir
    var zipFile = path.join(os.tmpdir(), 'hikaye-python-embed-' + PY_VERSION + '.zip');
    onLog('Python ' + PY_VERSION + ' indiriliyor (~15 MB)...');
    try {
      await httpDownload(PY_EMBED_URL, zipFile, function (p) { onLog('  indirme: %' + p); });
    } catch (e) {
      throw new Error('Python indirilemedi: ' + e.message + ' — İnternet bağlantını kontrol et.');
    }

    // 2) Çıkar
    onLog('Python çıkarılıyor...');
    fs.rmSync(BASE_DIR, { recursive: true, force: true });
    try {
      expandZip(zipFile, BASE_DIR);
    } finally {
      fs.rmSync(zipFile, { force: true });
    }

    if (!fs.existsSync(exe())) {
      throw new Error('Python çıkarılamadı: ' + exe() + ' bulunamadı.');
    }

    // 3) ._pth dosyasını düzelt (pip ve paketler için ZORUNLU)
    fixPthFile();
    onLog('Python ' + PY_VERSION + ' hazır.');
  }

  // 4) pip bootstrap (yoksa veya bozulmuşsa)
  if (!pipReady()) {
    onLog('pip kuruluyor...');
    var getPipPath = path.join(BASE_DIR, 'get-pip.py');
    var getPipBuf = await httpGetBuf(GET_PIP_URL);
    fs.writeFileSync(getPipPath, getPipBuf);
    try {
      await new Promise(function (resolve, reject) {
        var child = spawn(exe(), [getPipPath, '--no-warn-script-location'], {
          windowsHide: true,
          env: Object.assign({}, process.env, { PYTHONIOENCODING: 'utf-8' }),
        });
        child.stdout.on('data', function (d) {
          d.toString().split(/\r?\n/).filter(Boolean).forEach(function (l) { onLog('  ' + l); });
        });
        child.stderr.on('data', function (d) {
          d.toString().split(/\r?\n/).filter(Boolean).forEach(function (l) { onLog('  ' + l); });
        });
        child.once('error', reject);
        child.once('exit', function (code) {
          if (code === 0) resolve();
          else reject(new Error('get-pip başarısız (kod ' + code + ')'));
        });
      });
    } finally {
      fs.rmSync(getPipPath, { force: true });
    }
    onLog('pip kuruldu.');
  }

  onLog('Taşınabilir Python hazır: ' + exe());
  return exe();
}

module.exports = { exe: exe, installed: installed, pipReady: pipReady, setup: setup, BASE_DIR: BASE_DIR, PY_VERSION: PY_VERSION };
