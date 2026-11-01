$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$node = Join-Path $PSScriptRoot "node.exe"
$destination = Join-Path $root "mod\build\youtube_provider"
$version = "2.0.2"
$sourceUrl = "https://github.com/Brainicism/bgutil-ytdlp-pot-provider/archive/refs/tags/$version.zip"
$pluginUrl = "https://github.com/Brainicism/bgutil-ytdlp-pot-provider/releases/download/$version/bgutil-ytdlp-pot-provider.zip"
$sourceSha256 = "DF68EE6210E4960757BCEAE298927FB2EBD4D5CD9D390F9FF3A8287B0D2D1054"
$pluginSha256 = "A26B980BF6D8E8400D8F21445DAA4D8381BF6183D20F0BE309391F86B31E27EF"
$markerText = "bgutil-ytdlp-pot-provider $version"
$marker = Join-Path $destination "HIKAYE_PROVIDER_VERSION.txt"

if ((Test-Path $marker) -and
    (Get-Content -LiteralPath $marker -Raw).Trim() -eq $markerText -and
    (Test-Path (Join-Path $destination "server\build\main.js")) -and
    (Test-Path (Join-Path $destination "plugins\bgutil-ytdlp-pot-provider.zip"))) {
    Write-Host "YouTube PO Token provider $version is already prepared."
    return
}

if (-not (Test-Path $node)) { throw "Bundled Node.js runtime missing: $node" }
$nodeVersion = (& $node --version).Trim().TrimStart("v")
if ([version]$nodeVersion -lt [version]"22.0") {
    throw "The PO Token provider requires Node.js 22 or later; found $nodeVersion."
}
$npmCommand = Get-Command npm -ErrorAction SilentlyContinue
if (-not $npmCommand) {
    throw "npm 9 or later is required to prepare the PO Token provider."
}
$npmVersion = (& npm --version).Trim()
if ([version]$npmVersion -lt [version]"9.0") {
    throw "npm 9 or later is required to prepare the PO Token provider; found $npmVersion."
}

$temporaryRoot = Join-Path $env:TEMP ("hikaye-bgutil-" + [guid]::NewGuid().ToString("N"))
$sourceArchive = Join-Path $temporaryRoot "source.zip"
$pluginArchive = Join-Path $temporaryRoot "plugin.zip"
$sourceExtract = Join-Path $temporaryRoot "source"
$staging = Join-Path $temporaryRoot "runtime"
New-Item -ItemType Directory -Path $temporaryRoot -Force | Out-Null

try {
    Invoke-WebRequest -Uri $sourceUrl -OutFile $sourceArchive
    Invoke-WebRequest -Uri $pluginUrl -OutFile $pluginArchive
    if ((Get-FileHash -LiteralPath $sourceArchive -Algorithm SHA256).Hash -ne $sourceSha256) {
        throw "The provider source archive failed SHA-256 verification."
    }
    if ((Get-FileHash -LiteralPath $pluginArchive -Algorithm SHA256).Hash -ne $pluginSha256) {
        throw "The provider plugin archive failed SHA-256 verification."
    }

    Expand-Archive -LiteralPath $sourceArchive -DestinationPath $sourceExtract
    $providerSource = Join-Path $sourceExtract "bgutil-ytdlp-pot-provider-$version"
    $server = Join-Path $providerSource "server"
    if (-not (Test-Path (Join-Path $server "package-lock.json"))) {
        throw "The provider source archive has an unexpected layout."
    }

    Push-Location $server
    try {
        npm ci
        if ($LASTEXITCODE -ne 0) { throw "npm ci failed for the PO Token provider." }
        npx tsc
        if ($LASTEXITCODE -ne 0) { throw "The PO Token provider TypeScript build failed." }
        npm audit fix --omit=dev
        if ($LASTEXITCODE -ne 0) { throw "npm audit fix failed for provider production dependencies." }
        npm prune --omit=dev
        if ($LASTEXITCODE -ne 0) { throw "npm prune failed for the PO Token provider." }
        $auditOutput = npm audit --omit=dev --json 2>$null
        $auditExitCode = $LASTEXITCODE
        $audit = ($auditOutput -join [Environment]::NewLine) | ConvertFrom-Json
        if ($auditExitCode -ne 0 -or $audit.metadata.vulnerabilities.total -gt 0) {
            throw "Provider production dependencies still have npm audit findings."
        }
    } finally {
        Pop-Location
    }

    New-Item -ItemType Directory -Path $staging -Force | Out-Null
    Copy-Item -LiteralPath $server -Destination (Join-Path $staging "server") -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $providerSource "LICENSE") -Destination (Join-Path $staging "LICENSE") -Force
    Copy-Item -LiteralPath (Join-Path $providerSource "README.md") -Destination (Join-Path $staging "README.md") -Force
    $pluginDirectory = Join-Path $staging "plugins"
    New-Item -ItemType Directory -Path $pluginDirectory -Force | Out-Null
    Copy-Item -LiteralPath $pluginArchive `
        -Destination (Join-Path $pluginDirectory "bgutil-ytdlp-pot-provider.zip") -Force
    Set-Content -LiteralPath (Join-Path $staging "HIKAYE_PROVIDER_VERSION.txt") `
        -Value $markerText -Encoding ASCII

    if (Test-Path $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    Move-Item -LiteralPath $staging -Destination $destination
    Write-Host "YouTube PO Token provider $version prepared at $destination."
} finally {
    if (Test-Path $temporaryRoot) { Remove-Item -LiteralPath $temporaryRoot -Recurse -Force }
}
