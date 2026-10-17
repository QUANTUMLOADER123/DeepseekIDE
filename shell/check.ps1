# check.ps1 — самотест shell-хоста DeepSeek Extended: имитирует фрейм запроса
# Chrome и ждёт фрейм ответа от host.bat (из папки, где лежит что check.ps1).
param([string]$ExecFile)
$ErrorActionPreference = 'Continue'
if (-not $ExecFile) { $ExecFile = Join-Path $PSScriptRoot 'host.bat' }
Write-Host ("== selftest: " + $ExecFile)

if (-not (Test-Path $ExecFile)) { Write-Host 'FAIL: host.bat not found'; exit 2 }

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName  = 'cmd.exe'
$psi.Arguments = '/c "' + $ExecFile + '"'
$psi.WorkingDirectory = $PSScriptRoot
$psi.RedirectStandardInput  = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError  = $true
$psi.UseShellExecute = $false
$psi.CreateNoWindow  = $true

try { $p = [System.Diagnostics.Process]::Start($psi) }
catch { Write-Host ("FAIL: cannot start host: " + $_.Exception.Message); exit 3 }

$payload = '{"id":"selftest","cmd":"echo selftest-ok & cd"}'
$bytes   = [Text.Encoding]::UTF8.GetBytes($payload)
$lenBuf  = [BitConverter]::GetBytes([int]$bytes.Length)
$p.StandardInput.BaseStream.Write($lenBuf, 0, 4)
$p.StandardInput.BaseStream.Write($bytes, 0, $bytes.Length)
$p.StandardInput.BaseStream.Flush()
$p.StandardInput.Close()

if (-not $p.WaitForExit(20000)) {
    try { $p.Kill() } catch { }
    Write-Host ('FAIL: host hung (no reply in 20s). Likely PowerShell policy blocks host.ps1 — run:')
    Write-Host ('  powershell -ExecutionPolicy Bypass -File "' + (Join-Path $PSScriptRoot 'host.ps1') + '"')
    Write-Host 'and look at the error text, then tell the developer.'
    $er = $p.StandardError.ReadToEnd()
    if ($er) { Write-Host ('--- stderr ---`n' + $er) }
    exit 4
}
$raw = $p.StandardOutput.BaseStream.ReadToEnd()
$er  = $p.StandardError.ReadToEnd()
if (-not $raw -or $raw.Length -lt 4) {
    Write-Host ('FAIL: empty reply from host. stderr:`n' + $er)
    exit 5
}
$rlen = [BitConverter]::ToInt32($raw[0..3], 0)
$body = [Text.Encoding]::UTF8.GetString($raw[4..(4 + $rlen - 1)])
$resp = $body | ConvertFrom-Json
if ($resp.ok -and ($resp.stdout -match 'selftest-ok')) {
    Write-Host ('SELFTEST OK. stdout: ' + $resp.stdout.Trim() + ' | cwd: ' + $resp.cwd)
    exit 0
}
Write-Host ('FAIL: unexpected reply: ' + $body)
if ($er) { Write-Host ('--- stderr ---`n' + $er) }
exit 6
