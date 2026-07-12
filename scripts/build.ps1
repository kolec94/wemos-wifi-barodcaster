param(
    [string]$SummaryPath
)

$ErrorActionPreference = "Stop"

$fqbn = "esp8266:esp8266:d1_mini_lite"
$coreId = "esp8266:esp8266"
$coreVersion = "3.1.2"
$sketches = @(
    @{ Name = "Main firmware"; Path = "wemos-wifi-barodcaster" },
    @{ Name = "Beacon counter"; Path = "test-tools/beacon-counter" }
)

if (-not (Get-Command arduino-cli -ErrorAction SilentlyContinue)) {
    throw "arduino-cli is not installed or is not available on PATH."
}

$coreLine = arduino-cli core list | Where-Object { $_ -match "^$([regex]::Escape($coreId))\s+" }
if ($LASTEXITCODE -ne 0) {
    throw "Unable to query installed Arduino cores."
}

$installedVersion = if ($coreLine) { ($coreLine -split "\s+")[1] } else { $null }
if ($installedVersion -ne $coreVersion) {
    $foundVersion = if ($installedVersion) { $installedVersion } else { "none" }
    throw "Required core $coreId@$coreVersion is not installed (found: $foundVersion). See BUILDING.md."
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$measurements = @()
foreach ($sketch in $sketches) {
    $sketchPath = Join-Path $repoRoot $sketch.Path
    Write-Host "`n==> Compiling $($sketch.Path) ($fqbn, core $coreVersion)"
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        # ESP8266 writes its successful size report to stderr.
        $ErrorActionPreference = "Continue"
        $compileOutput = @(& arduino-cli compile --fqbn $fqbn $sketchPath 2>&1)
        $compileExitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    $compileOutput = @($compileOutput | ForEach-Object { $_.ToString() })
    $compileOutput | ForEach-Object { Write-Host $_ }
    if ($compileExitCode -ne 0) {
        throw "Compilation failed for $($sketch.Path)."
    }

    $outputText = $compileOutput -join "`n"
    $flashMatch = [regex]::Match(
        $outputText,
        'Code in flash[^\r\n]*used\s+(\d+)\s*/\s*(\d+)\s+bytes\s+\((\d+)%\)'
    )
    $iramMatch = [regex]::Match(
        $outputText,
        'Instruction RAM[^\r\n]*used\s+(\d+)\s*/\s*(\d+)\s+bytes\s+\((\d+)%\)'
    )
    if (-not $flashMatch.Success -or -not $iramMatch.Success) {
        throw "Compilation succeeded, but memory usage could not be parsed for $($sketch.Path)."
    }

    $measurements += [pscustomobject]@{
        Name         = $sketch.Name
        Path         = $sketch.Path
        FlashUsed    = [int]$flashMatch.Groups[1].Value
        FlashMaximum = [int]$flashMatch.Groups[2].Value
        FlashPercent = [int]$flashMatch.Groups[3].Value
        IramUsed     = [int]$iramMatch.Groups[1].Value
        IramMaximum  = [int]$iramMatch.Groups[2].Value
        IramPercent  = [int]$iramMatch.Groups[3].Value
    }
}

Write-Host "`n==> Firmware memory usage"
$measurements | Format-Table Name, @{ Label = "Flash"; Expression = { "$($_.FlashUsed)/$($_.FlashMaximum) ($($_.FlashPercent)%)" } }, @{ Label = "IRAM"; Expression = { "$($_.IramUsed)/$($_.IramMaximum) ($($_.IramPercent)%)" } } | Out-String | Write-Host

if ($SummaryPath) {
    $summary = @(
        "# Firmware memory usage",
        "",
        "Toolchain: Arduino CLI 1.5.1, ESP8266 core $coreVersion, FQBN ``$fqbn``",
        "",
        "| Sketch | Flash | IRAM |",
        "| --- | ---: | ---: |"
    )
    foreach ($measurement in $measurements) {
        $summary += "| $($measurement.Name) | $($measurement.FlashUsed) / $($measurement.FlashMaximum) ($($measurement.FlashPercent)%) | $($measurement.IramUsed) / $($measurement.IramMaximum) ($($measurement.IramPercent)%) |"
    }
    $summaryDirectory = Split-Path -Parent $SummaryPath
    if ($summaryDirectory) {
        New-Item -ItemType Directory -Path $summaryDirectory -Force | Out-Null
    }
    $summary | Set-Content -Path $SummaryPath
    Write-Host "Memory report written to $SummaryPath"
}

Write-Host "Both sketches compiled successfully."
