# SPDX-License-Identifier: MIT
# Run in the logged-in user's session. Use a local output directory.
# The probe has four seconds of idle before and after its GPU load.
param(
    [Parameter(Mandatory = $true)]
    [string]$Icd,
    [ValidateRange(1, 120)]
    [int]$Seconds = 10,
    [string]$Exe = (Join-Path $PSScriptRoot 'vkqueueprobe.exe'),
    [string]$Out = (Join-Path $env:TEMP ('vgd-queue-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff')))
)

$ErrorActionPreference = 'Stop'
if (Test-Path $Out) { throw 'The output directory must be new.' }
$icdPath = (Resolve-Path $Icd).Path
$exePath = (Resolve-Path $Exe).Path
New-Item -ItemType Directory $Out | Out-Null
$arguments = '"{0}" {1}' -f $icdPath, $Seconds
$p = Start-Process $exePath -ArgumentList $arguments -NoNewWindow -PassThru `
    -RedirectStandardOutput (Join-Path $Out 'stdout.log') `
    -RedirectStandardError (Join-Path $Out 'stderr.log')
$null = $p.Handle
$start = Get-Date
$samples = @()
$modules = @()
do {
    $p.Refresh()
    if (-not $p.HasExited -and -not $modules.Count) {
        $modules = @($p.Modules | Where-Object { $_.FileName -eq $icdPath } |
            Select-Object ModuleName, FileName)
    }
    $samples += @{
        Time = (Get-Date).ToString('o')
        Formatted = @(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine |
            Where-Object { $_.Name -like "pid_$($p.Id)_*" } | Select-Object Name, UtilizationPercentage)
        Raw = @(Get-CimInstance Win32_PerfRawData_GPUPerformanceCounters_GPUEngine |
            Where-Object { $_.Name -like "pid_$($p.Id)_*" } |
            Select-Object Name, UtilizationPercentage, Timestamp_Sys100NS, Frequency_Sys100NS)
    }
    Start-Sleep -Milliseconds 300
    $p.Refresh()
} while (-not $p.HasExited -and ((Get-Date) - $start).TotalSeconds -lt ($Seconds + 20))
$timeout = -not $p.HasExited
if ($timeout) { $p | Stop-Process -Force }
$p.Refresh()
@{
    Icd = $icdPath
    Hash = (Get-FileHash $icdPath -Algorithm SHA256).Hash
    Environment = @{ VN_PERF = $env:VN_PERF; VN_DEBUG = $env:VN_DEBUG }
    Pid = $p.Id
    Exit = $p.ExitCode
    Timeout = $timeout
    Modules = $modules
    Samples = $samples
} | ConvertTo-Json -Depth 7 | Set-Content (Join-Path $Out 'result.json') -Encoding UTF8
Write-Output $Out
if ($timeout -or $p.ExitCode -ne 0) { exit 1 }
