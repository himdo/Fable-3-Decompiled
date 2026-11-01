# run_with_timeout.ps1 <exe> [args...] [-Timeout <seconds>]
# Runs an exe (with its args), capturing stdout/stderr to out.txt/err.txt;
# kills it after the timeout so a blocking MessageBox (or hang) auto-closes.
# The exit code (EXIT=<n>) or TIMEOUT_KILLED is printed, followed by the
# captured output so a caller can grep for a success marker.
param(
    [Parameter(Position=0, Mandatory=$true)][string]$Exe,
    [Parameter(Position=1, ValueFromRemainingArguments=$true)][string[]]$Rest
)
$TimeoutSec = 15
$OutFile = "out.txt"
$ErrFile = "err.txt"
$args = @()
for ($i=0; $i -lt $Rest.Count; $i++) {
    if ($Rest[$i] -eq '-Timeout') { $TimeoutSec = [int]$Rest[$i+1]; $i++ }
    elseif ($Rest[$i] -eq '-OutFile') { $OutFile = $Rest[$i+1]; $i++ }
    elseif ($Rest[$i] -eq '-ErrFile') { $ErrFile = $Rest[$i+1]; $i++ }
    else { $args += $Rest[$i] }
}
# Resolve a relative $Exe against the current location (PowerShell's `cd`
# does not change the .NET CurrentDirectory that Start-Process uses).
$base = (Get-Location).Path
$exePath = if ([System.IO.Path]::IsPathRooted($Exe)) { $Exe } else { [System.IO.Path]::GetFullPath((Join-Path $base $Exe)) }
# Start fresh so a leftover file from a previous run is not mistaken for this run.
foreach ($f in @($OutFile, $ErrFile)) { if (Test-Path $f) { Remove-Item $f -Force } }
if ($args.Count -gt 0) {
    $p = Start-Process -FilePath $exePath -ArgumentList $args -WorkingDirectory $base `
            -NoNewWindow -PassThru -RedirectStandardOutput $OutFile -RedirectStandardError $ErrFile
} else {
    $p = Start-Process -FilePath $exePath -WorkingDirectory $base `
            -NoNewWindow -PassThru -RedirectStandardOutput $OutFile -RedirectStandardError $ErrFile
}
$done = $false
for ($i=0; $i -lt ($TimeoutSec * 10); $i++) {
    if ($p.HasExited) { $done = $true; break }
    Start-Sleep -Milliseconds 100
}
if (-not $done) {
    try { $p.Kill() } catch {}
    Write-Host "TIMEOUT_KILLED (no exit within ${TimeoutSec}s)"
} else {
    $ec = $p.ExitCode
    if ($null -eq $ec) { $ec = "?" }
    Write-Host ("EXIT=" + $ec)
}
if (Test-Path $OutFile) { Write-Host "=== STDOUT ==="; Get-Content $OutFile }
if (Test-Path $ErrFile) { Write-Host "=== STDERR ==="; Get-Content $ErrFile }
