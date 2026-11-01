<#
  HikayeTurkRP dağıtımı (FiveM mantığı: bağımsız kurulum, orijinal oyun dosyalarına dokunmaz).

  Çıktılar (dist\):
    installer\HikayeRP-Setup-<sürüm>.exe   KURULUM PROGRAMI: başlatıcı (Electron) + mod + veriler + Python sunucusu
                                           + Türkçe yama FARK dosyaları (~6 MB). Kullanıcı bunu çalıştırır.
    HikayeTurkRP-<sürüm>.zip               GÜNCELLEME paketi (mod, gamefiles, companion, tr-yama, bin, version.json): GitHub release'e
                                           yüklenir, başlatıcı "Güncellemeleri denetle" ile indirir. (Başlatıcının kendisi içermez.)

  TÜRKÇE YAMA: Rockstar'ın update.rpf/update2.rpf dosyaları pakete KONMAZ. tr-yama\ içinde sadece orijinal ile yamalı dosya arasındaki
  FARK (xdelta3, ~6 MB) vardır; başlatıcı yamalı dosyayı kullanıcının KENDİ orijinalinden üretir ve SHA-256 ile doğrular.
  Fark dosyalarını (yeniden) üretmek için:  .\tools\make_tr_delta.ps1   (oyun güncellenirse tekrar çalıştır)

  Pakete KONMAZ: Menyoo, OpenIV, ScriptHookV, oyunun mods\ klasörü, Rockstar dosyaları, kayıtlar (saves).

  Kullanım:  .\package.ps1                    # sürüm: version.json
             .\package.ps1 -Version 0.2.0     # version.json'u günceller
             .\package.ps1 -SkipInstaller     # sadece güncelleme zip'i (hızlı)
#>
param(
    [string]$Version,
    [switch]$SkipInstaller,
    [switch]$Keep,   # hata ayıklama: win-unpacked ve _payload silinmez
    [switch]$CheckVersion   # sadece version.json'dan sürüm bilgisini göster
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$verFile = Join-Path $root "version.json"
if ($Version) { [IO.File]::WriteAllText($verFile, ('{ "version": "' + $Version + '" }'), (New-Object System.Text.UTF8Encoding($false))) }
$ver = (Get-Content $verFile -Raw | ConvertFrom-Json).version
if ($CheckVersion) {
    Write-Host "Current version: $ver"
    exit
}
Write-Host "== HikayeTurkRP $ver paketleniyor =="
# package.json sürümünü eşitle (kurulum programı adı/sürümü buradan)
$pj = Join-Path $root "launcher\package.json"
$utf8 = New-Object System.Text.UTF8Encoding($false)   # BOM'suz UTF-8 (Windows PowerShell 5.1 varsayılanı bozar)
$pt = [IO.File]::ReadAllText($pj, $utf8)
$pt = [regex]::Replace($pt, '"version":\s*"[^"]*"', '"version": "' + $ver + '"', 1)
[IO.File]::WriteAllText($pj, $pt, $utf8)

# 1) Modu derle (oyuna KURULMAZ)
& (Join-Path $root "build.ps1") -NoInstall
$asi = Join-Path $root "mod\build\HikayeTurkRP.asi"
if (-not (Test-Path $asi)) { throw "HikayeTurkRP.asi yok." }
$cefRuntime = Join-Path $root "mod\build\cef"
$pythonHost = Join-Path $root "companion\python_browser_host.py"
if (Test-Path $pythonHost) {
    Write-Host "Python CEF host bulundu; native CEF runtime dosyaları zorunlu değil."
    $usePythonCef = $true
} else {
    $usePythonCef = $false
    if (-not (Test-Path (Join-Path $cefRuntime "libcef.dll")) -or
        -not (Test-Path (Join-Path $cefRuntime "HikayeTurkRPSubprocess.exe")) -or
        -not (Test-Path (Join-Path $cefRuntime "HikayeTurkRPBrowserHost.exe"))) {
        throw "CEF runtime bulunamadı. build.ps1 CEF derlemesini tamamlamadı."
    }
}

# 2) Yük (payload): kurulum programına ve güncelleme zip'ine girecek dosyalar
$dist = Join-Path $root "dist"
$pay = Join-Path $dist "_payload"
if (Test-Path $pay) { Remove-Item $pay -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $pay "mod") | Out-Null
Copy-Item $asi (Join-Path $pay "mod\HikayeTurkRP.asi")
# version.json (zip kökünde olmalı - başlatıcı güncelleme kontrolü için)
Copy-Item (Join-Path $root "version.json") (Join-Path $pay "version.json") -Force
# python_browser_host.py (companion CEF host için) - companion/ klasöründen
if (Test-Path (Join-Path $root "companion\python_browser_host.py")) {
    Copy-Item (Join-Path $root "companion\python_browser_host.py") (Join-Path $pay "mod\python_browser_host.py") -Force
}
# Copy native CEF runtime only if it exists (Python host may be used instead)
if (Test-Path $cefRuntime) {
    robocopy $cefRuntime (Join-Path $pay "mod\cef") /E /NFL /NDL /NJH /NJS /NP | Out-Null
} else {
    Write-Host "Native CEF runtime klasörü bulunamadı; Python CEF host kullanılacak."
}
$gf = Join-Path $pay "gamefiles\HikayeTurkRP"
New-Item -ItemType Directory -Force $gf | Out-Null
robocopy (Join-Path $root "data") (Join-Path $gf "data") /E /NFL /NDL /NJH /NJS /NP | Out-Null
robocopy (Join-Path $root "addons") (Join-Path $gf "addons") /E /NFL /NDL /NJH /NJS /NP | Out-Null
$portableVlc = Join-Path $root "tools\vlc"
if (Test-Path (Join-Path $portableVlc "libvlc.dll")) {
    robocopy $portableVlc (Join-Path $gf "tools\vlc") /E /NFL /NDL /NJH /NJS /NP | Out-Null
}
foreach ($file in @("yt-dlp.exe", "yt-dlp.LICENSE", "node.exe", "node.LICENSE")) {
    $source = Join-Path $root ("tools\" + $file)
    if (-not (Test-Path $source)) { throw "YouTube runtime file missing: $source" }
    New-Item -ItemType Directory -Force (Join-Path $gf "tools") | Out-Null
    Copy-Item $source (Join-Path $gf ("tools\" + $file)) -Force
}
$youtubeProviderDir = Join-Path $root "mod\build\youtube_provider"
if (-not (Test-Path (Join-Path $youtubeProviderDir "server\build\main.js"))) {
    throw "YouTube PO Token provider runtime missing: $youtubeProviderDir"
}
$packageProviderDir = Join-Path $gf "tools\bgutil"
New-Item -ItemType Directory -Force $packageProviderDir | Out-Null
Copy-Item (Join-Path $youtubeProviderDir "*") $packageProviderDir -Recurse -Force
$radRuntime = Join-Path $root "mod\third_party\radruntime"
foreach ($file in @("radvideo64.exe", "binkplay.exe", "smackplw.exe")) {
    if (-not (Test-Path (Join-Path $radRuntime $file))) {
        throw "RAD VideoTools runtime file missing: $(Join-Path $radRuntime $file)"
    }
}
$radRuntimeTarget = Join-Path $gf "tools\radruntime"
New-Item -ItemType Directory -Force $radRuntimeTarget | Out-Null
foreach ($file in @("radvideo64.exe", "binkplay.exe", "smackplw.exe")) {
    Copy-Item (Join-Path $radRuntime $file) (Join-Path $radRuntimeTarget $file) -Force
}
robocopy (Join-Path $root "companion") (Join-Path $pay "companion") /E /XD __pycache__ tests .pytest_cache /XF *.pyc /NFL /NDL /NJH /NJS /NP | Out-Null

# tr-yama (Türkçe yama fark dosyaları)
if (Test-Path (Join-Path $root "tr-yama")) {
    robocopy (Join-Path $root "tr-yama") (Join-Path $pay "tr-yama") /E /NFL /NDL /NJH /NJS /NP | Out-Null
}

# bin (xdelta3.exe vb. araçlar) - tools/bin'den
if (Test-Path (Join-Path $root "tools\bin")) {
    robocopy (Join-Path $root "tools\bin") (Join-Path $pay "bin") /E /NFL /NDL /NJH /NJS /NP | Out-Null
}

# vendor (ScriptHookV, OpenIV, dinput8.dll) - shoiv'den (proje kökünde)
$shoivVendor = Join-Path $root "shoiv"
if (Test-Path $shoivVendor) {
    robocopy $shoivVendor (Join-Path $pay "vendor") /E /NFL /NDL /NJH /NJS /NP | Out-Null
} else {
    # Fallback: mod/build/vendor'den (derleme sonrası oluşur)
    $buildVendor = Join-Path $root "mod\build\vendor"
    if (Test-Path $buildVendor) {
        robocopy $buildVendor (Join-Path $pay "vendor") /E /NFL /NDL /NJH /NJS /NP | Out-Null
    } else {
        # Fallback: launcher'ın vendor klasöründen (geliştirme modunda)
        $launcherVendor = Join-Path $root "launcher\vendor"
        if (Test-Path $launcherVendor) {
            robocopy $launcherVendor (Join-Path $pay "vendor") /E /NFL /NDL /NJH /NJS /NP | Out-Null
        }
    }
}

# README.txt (PAKET_README.txt -> README.txt)
if (Test-Path (Join-Path $root "PAKET_README.txt")) {
    Copy-Item (Join-Path $root "PAKET_README.txt") (Join-Path $pay "README.txt") -Force
}

# THIRD_PARTY.md
if (Test-Path (Join-Path $root "THIRD_PARTY.md")) {
    Copy-Item (Join-Path $root "THIRD_PARTY.md") (Join-Path $pay "THIRD_PARTY.md") -Force
}

# Verify CEF runtime presence; if Python host is used, native files may be absent.
if ($usePythonCef) {
    Write-Host "Python CEF host bulundu; native CEF runtime dosyaları zorunlu değil."
} else {
    throw "CEF runtime bulunamadı. build.ps1 CEF derlemesini tamamlamadı."
}


# 3) Güncelleme zip'i (başlatıcının GitHub güncellemesi bunu indirir)
$upd = Join-Path $dist "HikayeTurkRP-$ver.zip"
if (Test-Path $upd) { Remove-Item $upd -Force }
Compress-Archive -Path (Join-Path $pay "*") -DestinationPath $upd -CompressionLevel Optimal
Write-Host ("Güncelleme zip: {0} ({1:N0} MB)" -f $upd, ((Get-Item $upd).Length / 1MB))

# 4) Kurulum programı (Electron + NSIS)
if (-not $SkipInstaller) {
    $ldir = Join-Path $root "launcher"
    Push-Location $ldir
    try {
        $ErrorActionPreference = "Continue"   # npm/builder bilgi mesajlarını stderr'e yazar; çıkış kodunu kendimiz denetleriz
        if (-not (Test-Path "node_modules\electron-builder")) { npm install --no-audit --no-fund 2>&1 | Out-Host; if ($LASTEXITCODE -ne 0) { throw "npm install başarısız" } }
        foreach ($js in @("main.js", "core.js", "instance.js", "preload.js", "portable-python.js", "renderer\renderer.js")) {   # bozuk dosya paketlenmesin
            node --check $js 2>&1 | Out-Host
            if ($LASTEXITCODE -ne 0) { throw "$js sözdizimi hatası" }
        }
        # node test.js 2>&1 | Out-Host
        # if ($LASTEXITCODE -ne 0) { throw "launcher testleri başarısız" }
        $out = Join-Path $dist "installer"
        if (Test-Path $out) { Remove-Item $out -Recurse -Force }
        & (Join-Path $ldir "node_modules\.bin\electron-builder.cmd") --win nsis --x64 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "electron-builder başarısız" }
        $ErrorActionPreference = "Stop"
    } finally { Pop-Location }
    $setup = Get-ChildItem (Join-Path $dist "installer") -Filter "*Setup*.exe" | Select-Object -First 1
    if (-not $setup) { throw "Kurulum programı üretilemedi." }
    Write-Host ("Kurulum programı: {0} ({1:N0} MB)" -f $setup.FullName, ($setup.Length / 1MB))
    # açılmış klasörü (win-unpacked) temizle: kalabalık yapmasın
    $unp = Join-Path $dist "installer\win-unpacked"
    if ((Test-Path $unp) -and -not $Keep) { Remove-Item $unp -Recurse -Force }
}

if (-not $Keep) { Remove-Item $pay -Recurse -Force }
Write-Host "Tamam. Çıktılar: $dist"
