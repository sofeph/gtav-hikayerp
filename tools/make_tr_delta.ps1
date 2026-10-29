<#
  Türkçe yama FARK YAMASI üretir (xdelta3/VCDIFF). 2,4 GB'lık Rockstar verisi (update.rpf/update2.rpf) DAĞITILMAZ; sadece
  orijinal dosya ile Türkçe yamalı dosya arasındaki fark (birkaç MB) paketlenir. Kullanıcının bilgisayarında, kendi orijinal
  update.rpf/update2.rpf dosyasından yamalı dosya yeniden üretilir ve SHA-256 ile doğrulanır.

  Girdi : orijinal GTA V klasörü (update\update.rpf, update2.rpf)  +  "tr yama\mods\update\*.rpf" (yamalı sürüm)
  Çıktı : tr-yama\manifest.json, tr-yama\update.xd3, tr-yama\update2.xd3

  Kullanım: .\tools\make_tr_delta.ps1                       # oyun klasörü otomatik (Steam)
            .\tools\make_tr_delta.ps1 -GameDir "D:\GTAV"
  Yama yeni bir oyun sürümüne uyarlanırsa (Steam güncellemesi) bu betik yeniden çalıştırılır.
#>
param(
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto V",
    [string]$Patched = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Patched) { $Patched = Join-Path $root "tr yama\mods\update" }
$xd = Join-Path $root "tools\bin\xdelta3.exe"
if (-not (Test-Path $xd)) { throw "tools\bin\xdelta3.exe yok (github.com/jmacd/xdelta-gpl/releases)." }
$out = Join-Path $root "tr-yama"
New-Item -ItemType Directory -Force $out | Out-Null
$files = @()
foreach ($n in @("update", "update2")) {
    $src = Join-Path $GameDir "update\$n.rpf"
    $dst = Join-Path $Patched "$n.rpf"
    if (-not (Test-Path $src)) { throw "Orijinal yok: $src" }
    if (-not (Test-Path $dst)) { Write-Host "(yamalı $n.rpf yok, atlandı)"; continue }
    $delta = Join-Path $out "$n.xd3"
    if (Test-Path $delta) { Remove-Item $delta -Force }
    Write-Host "Fark üretiliyor: $n.rpf ..."
    $ErrorActionPreference = "Continue"
    & $xd -e -9 -B 536870912 -I 0 -f -s $src $dst $delta 2>&1 | Out-Null
    $code = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    if ($code -ne 0) { throw "xdelta3 başarısız ($n)" }
    $files += [ordered]@{
        name        = "$n.rpf"
        delta       = "$n.xd3"
        src_size    = (Get-Item $src).Length
        src_sha256  = (Get-FileHash $src -Algorithm SHA256).Hash
        out_size    = (Get-Item $dst).Length
        out_sha256  = (Get-FileHash $dst -Algorithm SHA256).Hash
        delta_size  = (Get-Item $delta).Length
    }
    Write-Host ("  {0}: fark {1:N1} MB" -f $n, ((Get-Item $delta).Length / 1MB))
}
$verFile = Join-Path $GameDir "version.txt"
$manifest = [ordered]@{
    _not   = "Türkçe yama fark yaması. update.rpf/update2.rpf kullanıcının KENDİ orijinal dosyasından üretilir (xdelta3). Oyun sürümü uyuşmazsa üretilmez."
    game_version = $(if (Test-Path $verFile) { (Get-Content $verFile -Raw).Trim() } else { "" })
    files  = $files
}
[IO.File]::WriteAllText((Join-Path $out "manifest.json"), ($manifest | ConvertTo-Json -Depth 5), (New-Object System.Text.UTF8Encoding($false)))
Write-Host "Tamam: $out"
