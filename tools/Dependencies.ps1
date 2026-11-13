function Restore-Source([object]$Dependency, [string]$Cache, [bool]$Offline, [string]$CMake) {
    $source = Join-Path $Cache $Dependency.directory
    $marker = Join-Path $source '.source-sha256'
    if ((Test-Path -LiteralPath $marker) -and
        (Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt')) -and
        ((Get-Content -LiteralPath $marker -Raw).Trim() -eq $Dependency.sha256)) {
        Write-Host "Using restored $($Dependency.directory)"
        return $source
    }

    New-Item -ItemType Directory -Path $Cache -Force | Out-Null
    $archive = Join-Path $Cache $Dependency.archive
    if (!(Test-Path -LiteralPath $archive)) {
        if ($Offline) { throw "Offline archive is missing: $archive" }
        $previousTls = [Net.ServicePointManager]::SecurityProtocol
        try {
            [Net.ServicePointManager]::SecurityProtocol = $previousTls -bor [Net.SecurityProtocolType]::Tls12
            Write-Host "Downloading $($Dependency.archive)"
            Invoke-WebRequest -UseBasicParsing -Uri $Dependency.url -OutFile $archive -TimeoutSec 180
        } finally { [Net.ServicePointManager]::SecurityProtocol = $previousTls }
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $Dependency.sha256) {
        throw "Dependency checksum mismatch: $archive"
    }

    Write-Host "Restoring $($Dependency.directory)"
    $timer = [Diagnostics.Stopwatch]::StartNew()
    if ($Dependency.archive.EndsWith('.zip')) {
        Expand-Archive -LiteralPath $archive -DestinationPath $Cache -Force
    } else {
        # Use CMake's bundled archive reader. Windows Server's tar may invoke
        # an external bzip2 from PATH, which can hang on CI runners.
        Write-Host "Extracting with CMake: $CMake"
        Invoke-Checked $CMake @('-E', 'chdir', $Cache, $CMake, '-E', 'tar', 'xf', $archive)
    }
    if (!(Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt'))) {
        throw "Dependency extraction did not produce the expected source directory: $source"
    }
    Set-Content -LiteralPath $marker -Value $Dependency.sha256 -Encoding ascii
    Write-Host ("Restored {0} in {1:N1}s" -f $Dependency.directory, $timer.Elapsed.TotalSeconds)
    return $source
}
