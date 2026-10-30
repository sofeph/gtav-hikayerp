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
    [switch]$Keep   # hata ayıklama: win-unpacked ve _payload silinmez
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$verFile = Join-Path $root "version.json"
if ($Version) { [IO.File]::WriteAllText($verFile, ('{ "version": "' + $Version + '" }'), (New-Object System.Text.UTF8Encoding($false))) }
$ver = (Get-Content $verFile -Raw | ConvertFrom-Json).version
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
if (-not (Test-Path (Join-Path $cefRuntime "libcef.dll")) -or
    -not (Test-Path (Join-Path $cefRuntime "HikayeTurkRPSubprocess.exe")) -or
    -not (Test-Path (Join-Path $cefRuntime "HikayeTurkRPBrowserHost.exe"))) {
    throw "CEF runtime bulunamadı. build.ps1 CEF derlemesini tamamlamadı."
}

# 2) Yük (payload): kurulum programına ve güncelleme zip'ine girecek dosyalar
$dist = Join-Path $root "dist"
$pay = Join-Path $dist "_payload"
if (Test-Path $pay) { Remove-Item $pay -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $pay "mod") | Out-Null
Copy-Item $asi (Join-Path $pay "mod\HikayeTurkRP.asi")
robocopy $cefRuntime (Join-Path $pay "mod\cef") /E /NFL /NDL /NJH /NJS /NP | Out-Null
$gf = Join-Path $pay "gamefiles\HikayeTurkRP"
New-Item -ItemType Directory -Force $gf | Out-Null
robocopy (Join-Path $root "data") (Join-Path $gf "data") /E /NFL /NDL /NJH /NJS /NP | Out-Null
robocopy (Join-Path $root "addons") (Join-Path $gf "addons") /E /NFL /NDL /NJH /NJS /NP | Out-Null
robocopy (Join-Path $root "companion") (Join-Path $pay "companion") /E /XD __pycache__ tests .pytest_cache /XF *.pyc /NFL /NDL /NJH /NJS /NP | Out-Null

# Türkçe yama: fark dosyaları + xdelta3 (Rockstar dosyaları DEĞİL)
New-Item -ItemType Directory -Force (Join-Path $pay "tr-yama"), (Join-Path $pay "bin") | Out-Null
$trd = Join-Path $root "tr-yama"
if (Test-Path (Join-Path $trd "manifest.json")) {
    robocopy $trd (Join-Path $pay "tr-yama") /E /NFL /NDL /NJH /NJS /NP | Out-Null
    Copy-Item (Join-Path $root "tools\bin\xdelta3.exe") (Join-Path $pay "bin\xdelta3.exe")
    Set-Content (Join-Path $pay "bin\XDELTA3-LISANS.txt") "xdelta3 3.1.0 (c) Joshua MacDonald, GPL-2.0-or-later. Kaynak: https://github.com/jmacd/xdelta-gpl/releases/tag/v3.1.0 . Ayri bir arac olarak, degistirilmeden calistirilir." -Encoding ASCII
} else {
    Write-Host "UYARI: tr-yama\manifest.json yok: Türkçe yama pakete girmedi (tools\make_tr_delta.ps1 çalıştır)."
}
# ScriptHookV / OpenIV dosyaları (shoiv\): kurulum programına girer, oyun KOPYASINA konur (orijinale değil)
$shoiv = Join-Path $root "shoiv"
if (Test-Path $shoiv) {
    New-Item -ItemType Directory -Force (Join-Path $pay "vendor") | Out-Null
    Copy-Item (Join-Path $shoiv "*") (Join-Path $pay "vendor") -Force
} else { Write-Host "UYARI: shoiv\ klasörü yok: ScriptHookV/OpenIV pakete girmedi." }
# paket sürümüne yapı damgası: aynı sürümü yeniden kuran kullanıcıda çalışma klasörü (Pkg) yenilenir
$vj = Get-Content $verFile -Raw | ConvertFrom-Json
$vjOut = @{ version = $vj.version; build = (Get-Date -Format "yyyyMMddHHmmss") } | ConvertTo-Json -Compress
[IO.File]::WriteAllText((Join-Path $pay "version.json"), $vjOut, (New-Object System.Text.UTF8Encoding($false)))


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
        node test.js 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "launcher testleri başarısız" }
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
