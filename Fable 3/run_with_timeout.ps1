# run_with_timeout.ps1 <exe> [args...] [-Timeout <seconds>]
# Runs an exe (with its args) capturing stdout/stderr; kills it after the
# timeout so a blocking MessageBox (or hang) auto-closes.
param(
    [Parameter(Position=0, Mandatory=$true)][string]$Exe,
    [Parameter(Position=1, ValueFromRemainingArguments=$true)][string[]]$Rest
)
$TimeoutSec = 15
$args = @()
for ($i=0; $i -lt $Rest.Count; $i++) {
    if ($Rest[$i] -eq '-Timeout') { $TimeoutSec = [int]$Rest[$i+1]; $i++ }
    else { $args += $Rest[$i] }
}
$psi = New-Object System.Diagnostics.ProcessStartInfo
# Resolve a relative $Exe against the current location (PowerShell's `cd`
# does not change the .NET CurrentDirectory that ProcessStartInfo uses).
$psi.FileName = (Resolve-Path $Exe).Path
$psi.WorkingDirectory = (Get-Location).Path
foreach ($a in $args) { $psi.ArgumentList.Add($a) }
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.CreateNoWindow = $true
$p = [System.Diagnostics.Process]::Start($psi)
$outTask = $p.StandardOutput.ReadToEndAsync()
$errTask = $p.StandardError.ReadToEndAsync()
$ok = $p.WaitForExit($TimeoutSec * 1000)
if (-not $ok) {
    try { $p.Kill() } catch {}
    Write-Host "TIMEOUT_KILLED (no exit within ${TimeoutSec}s)"
} else {
    Write-Host ("EXIT=" + $p.ExitCode)
}
if ($outTask.Wait()) { $out = $outTask.Result } else { $out = '' }
if ($errTask.Wait()) { $err = $errTask.Result } else { $err = '' }
Write-Host "=== STDOUT ==="
Write-Host $out
Write-Host "=== STDERR ==="
Write-Host $err
