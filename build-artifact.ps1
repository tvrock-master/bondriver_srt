#Requires -Version 5.1
[CmdletBinding()]
param([switch]$Offline, [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Test by default; tag releases use binaries built without the test targets.
foreach ($architecture in @('x64', 'x86')) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Architecture $architecture -Configuration Release -Test:(!$SkipTests) -Offline:$Offline
}

$distribution = Join-Path $PSScriptRoot 'dist'
$staging = Join-Path $distribution ('.package-' + [Guid]::NewGuid().ToString('N'))
$package = Join-Path $staging 'BonDriver_SRT'
$archive = Join-Path $distribution 'BonDriver_SRT.zip'
try {
    foreach ($architecture in @('x64', 'x86')) {
        $source = Join-Path $distribution "$architecture/Release"
        $destination = Join-Path $package $architecture
        New-Item -ItemType Directory -Path (Join-Path $destination 'licenses') -Force | Out-Null
        foreach ($file in @('BonDriver_SRT.dll', 'BonDriver_SRT.ini.example', 'README.md', 'LICENSE', 'THIRD_PARTY.md', 'licenses/SRT.txt', 'licenses/MbedTLS.txt')) {
            Copy-Item -LiteralPath (Join-Path $source $file) -Destination (Join-Path $destination $file) -Force
        }
        $settings = Get-Content -LiteralPath (Join-Path $destination 'BonDriver_SRT.ini.example') -Raw
        $settings = $settings -replace '(?m)^ListenAddress=[^\r\n]*', 'ListenAddress=0.0.0.0'
        Set-Content -LiteralPath (Join-Path $destination 'BonDriver_SRT.ini') -Value $settings -Encoding ascii -NoNewline
    }
    Compress-Archive -LiteralPath $package -DestinationPath $archive -Force
} finally {
    # Delete only this invocation's staging directory inside dist.
    $resolvedStaging = [IO.Path]::GetFullPath($staging)
    $allowedPrefix = [IO.Path]::GetFullPath($distribution).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar + '.package-'
    if (!$resolvedStaging.StartsWith($allowedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unexpected staging directory: $resolvedStaging"
    }
    if (Test-Path -LiteralPath $resolvedStaging) { Remove-Item -LiteralPath $resolvedStaging -Recurse -Force }
}
Write-Output "Release archive: $archive"
