<#
  GitHub sürümü yayınlar: dist\HikayeTurkRP-<sürüm>.zip dosyasını release olarak yükler (başlatıcı bunu "Güncellemeleri denetle" ile bulur).

  Gerekenler: önce .\package.ps1 çalıştırılmış olmalı; $env:GITHUB_TOKEN (repo/contents yazma izinli, "fine-grained" önerilir).
  Kullanım:   $env:GITHUB_TOKEN = "..."
              .\tools\publish_release.ps1 -Repo kullanici/depo -Notes "Neler yeni"
              .\tools\publish_release.ps1 -Repo kullanici/depo -IncludeTrPatch   # TR yama parçalarını da yükler (her biri <2 GB olmalı)
  Anahtar dosyaya yazılmaz, günlüğe basılmaz.
#>
param(
    [Parameter(Mandatory = $true)][string]$Repo,
    [string]$Notes = "",
    [switch]$IncludeTrPatch,
    [switch]$Draft
)
$ErrorActionPreference = "Stop"
if ($Repo -notmatch '^[A-Za-z0-9][A-Za-z0-9-]*/[A-Za-z0-9_.-]+$') { throw "Depo 'kullanici/depo' biçiminde olmalı." }
if (-not $env:GITHUB_TOKEN) { throw "`$env:GITHUB_TOKEN tanımlı değil." }
$root = Split-Path $PSScriptRoot -Parent
$ver = (Get-Content (Join-Path $root "version.json") -Raw | ConvertFrom-Json).version
$tag = "v$ver"
$files = @(Join-Path $root "dist\HikayeTurkRP-$ver.zip")
if ($IncludeTrPatch) { $files += Get-ChildItem (Join-Path $root "dist") -Filter "HikayeTurkRP-TRYama-$ver-*.zip" | ForEach-Object FullName }
foreach ($f in $files) {
    if (-not (Test-Path $f)) { throw "Dosya yok: $f (önce package.ps1)" }
    if ((Get-Item $f).Length -ge 2GB) { throw "$f 2 GB sınırını aşıyor (GitHub release dosya sınırı)." }
}
$h = @{ Authorization = "Bearer $env:GITHUB_TOKEN"; Accept = "application/vnd.github+json"; "User-Agent" = "HikayeTurkRP-Publisher" }
$body = @{ tag_name = $tag; name = "Hikaye RP $ver"; body = $Notes; draft = [bool]$Draft; prerelease = $false } | ConvertTo-Json
$rel = Invoke-RestMethod -Method Post -Uri "https://api.github.com/repos/$Repo/releases" -Headers $h -Body $body -ContentType "application/json; charset=utf-8"
Write-Host "Sürüm oluşturuldu: $($rel.html_url)"
foreach ($f in $files) {
    $name = [IO.Path]::GetFileName($f)
    $up = "https://uploads.github.com/repos/$Repo/releases/$($rel.id)/assets?name=$([uri]::EscapeDataString($name))"
    Write-Host "Yükleniyor: $name"
    Invoke-RestMethod -Method Post -Uri $up -Headers ($h + @{ "Content-Type" = "application/zip" }) -InFile $f | Out-Null
}
Write-Host "Tamam."
