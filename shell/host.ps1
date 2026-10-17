# =====================================================================
# host.ps1 — native messaging host DeepSeek Extended (PowerShell 5.1+).
# Chrome spawn'ит host.bat → этот скрипт. Протокол stdio:
#   запрос:  4 байта little-endian длины + UTF-8 JSON  { id, cmd }
#   ответ:   тот же фрейминг                            { ok, stdout, stderr, exitCode, killed }
# Рабочая папка команд: shell\project.cwd (пишет install.bat) либо папка хоста.
# =====================================================================
$ErrorActionPreference = 'Continue'
$in  = [Console]::OpenStandardInput()
$out = [Console]::OpenStandardOutput()

function Read-Frame {
    $lenBuf = New-Object byte[] 4
    if ($in.Read($lenBuf, 0, 4) -lt 4) { return $null }
    $len = [BitConverter]::ToInt32($lenBuf, 0)
    if ($len -le 0 -or $len -gt 1048576) { return $null }
    $buf = New-Object byte[] $len
    $off = 0
    while ($off -lt $len) {
        $r = $in.Read($buf, $off, $len - $off)
        if ($r -le 0) { return $null }
        $off += $r
    }
    return [Text.Encoding]::UTF8.GetString($buf)
}

function Write-Frame([string]$s) {
    $bytes  = [Text.Encoding]::UTF8.GetBytes($s)
    $lenBuf = [BitConverter]::GetBytes([int]$bytes.Length)
    $out.Write($lenBuf, 0, 4)
    $out.Write($bytes, 0, $bytes.Length)
    $out.Flush()
}

# рабочая папка для команд модели
$workingDir = $PSScriptRoot
try {
    $cwdFile = Join-Path $PSScriptRoot 'project.cwd'
    if (Test-Path $cwdFile) {
        $w = (Get-Content $cwdFile -Raw -ErrorAction SilentlyContinue).Trim()
        if ($w -and (Test-Path $w)) { $workingDir = $w }
    }
} catch { }

while ($true) {
    $frame = Read-Frame
    if ($null -eq $frame) { break }
    try {
        $req = $frame | ConvertFrom-Json
        $cmd = [string]$req.cmd
        if (-not $cmd.Trim()) { throw 'пустая команда' }

        $stdoutFile = [IO.Path]::GetTempFileName()
        $stderrFile = [IO.Path]::GetTempFileName()
        $killed = $false
        try {
            $p = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', $cmd `
                  -WorkingDirectory $workingDir -NoNewWindow -PassThru `
                  -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile
            if (-not $p.WaitForExit(120000)) { $p.Kill(); $killed = $true }
            $code = $(if ($killed) { -1 } else { $p.ExitCode })
        } catch {
            $code = -1; $killed = $true
        }
        $stdout = ''; $stderr = ''
        try { $stdout = [IO.File]::ReadAllText($stdoutFile) } catch { }
        try { $stderr = [IO.File]::ReadAllText($stderrFile) } catch { }
        Remove-Item $stdoutFile, $stderrFile -Force -ErrorAction SilentlyContinue
        if ($stdout.Length -gt 65536) { $stdout = $stdout.Substring(0, 65536) + "`n...(truncated)" }
        if ($stderr.Length -gt 16384) { $stderr = $stderr.Substring(0, 16384) + "`n...(truncated)" }
        $resp = [ordered]@{ ok = $true; stdout = $stdout; stderr = $stderr; exitCode = $code; killed = $killed; cwd = $workingDir }
        Write-Frame ($resp | ConvertTo-Json -Compress)
    } catch {
        Write-Frame (@{ ok = $false; error = $_.Exception.Message } | ConvertTo-Json -Compress)
    }
}
