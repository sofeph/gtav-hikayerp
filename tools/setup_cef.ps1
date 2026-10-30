param(
    [string]$Destination = (Join-Path $PSScriptRoot "..\mod\third_party\cef")
)

$ErrorActionPreference = "Stop"
$version = "156.0.14+g4fa4ab5+chromium-156.0.8078.48"
$archiveName = "cef_binary_${version}_windows64.tar.bz2"
$archiveUrl = "https://cef-builds.spotifycdn.com/$archiveName"
$expectedSize = 367176591
$expectedSha1 = "7a1049f0facf3b6ce9efc4d683cf1bac72691ba6"
$cefRootName = "cef_binary_${version}_windows64"
$destination = [IO.Path]::GetFullPath($Destination)
$archive = Join-Path $destination $archiveName
$sdkRoot = Join-Path $destination "sdk"
$cefRoot = Join-Path $sdkRoot $cefRootName

New-Item -ItemType Directory -Force $destination, $sdkRoot | Out-Null
if (-not (Test-Path $archive)) {
    curl.exe -L --fail --show-error --output $archive $archiveUrl
    if ($LASTEXITCODE -ne 0) { throw "CEF arşivi indirilemedi: $archiveUrl" }
}

$file = Get-Item $archive
if ($file.Length -ne $expectedSize) { throw "CEF arşiv boyutu beklenmiyor: $($file.Length)" }
$hash = (Get-FileHash $archive -Algorithm SHA1).Hash.ToLowerInvariant()
if ($hash -ne $expectedSha1) { throw "CEF SHA1 doğrulaması başarısız: $hash" }

if (-not (Test-Path (Join-Path $cefRoot "libcef_dll\CMakeLists.txt"))) {
    tar.exe -xf $archive -C $sdkRoot
    if ($LASTEXITCODE -ne 0) { throw "CEF arşivi açılamadı." }
}

if (-not (Test-Path (Join-Path $cefRoot "Release\libcef.lib"))) {
    throw "CEF SDK eksik veya hatalı: $cefRoot"
}
Write-Host "CEF $version doğrulandı: $cefRoot"
