# Nap firmware + mo idf monitor, dong thoi ghi log THOI GIAN THUC vao logs\<yyyyMMdd-HHmmss>_fdictdm10<4 hex SHA256 ELF>.log
# Dung:   .\tools\flash-monitor.ps1            (nap roi monitor)
#         .\tools\flash-monitor.ps1 -NoFlash   (chi monitor)
# Mo cua so moi:
#   Start-Process powershell -ArgumentList '-NoExit','-ExecutionPolicy','Bypass','-File','<repo>\tools\flash-monitor.ps1'
# Thoat monitor: Ctrl+]
param(
    [switch]$NoFlash,
    [string]$Port = 'COM6',
    # moi truong ESP-IDF (bo qua neu idf.py da co trong PATH)
    [string]$IdfProfile = 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
)

$Repo = Split-Path -Parent $PSScriptRoot
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue) -and (Test-Path $IdfProfile)) { . $IdfProfile }
Set-Location $Repo

# COM chi mot chuong trinh dung duoc mot luc: khong mo neu monitor/esptool khac dang giu cong
$busy = Get-CimInstance Win32_Process | Where-Object {
    $_.ProcessId -ne $PID -and $_.CommandLine -and
    $_.CommandLine -match '(idf_monitor|esp_idf_monitor|esptool)' -and $_.CommandLine -match "\b$Port\b"
}
if ($busy) {
    Write-Host "$Port dang bi chiem boi:" -ForegroundColor Yellow
    $busy | ForEach-Object { Write-Host ("  PID {0} {1}" -f $_.ProcessId, $_.Name) }
    Write-Host 'Dong monitor do (Ctrl+]) roi chay lai.' -ForegroundColor Yellow
    exit 1
}

$desc = Get-Content (Join-Path $Repo 'build\project_description.json') -Raw | ConvertFrom-Json
$bin = Get-Item (Join-Path $Repo ('build\' + $desc.app_bin))
$elfName = if ($desc.app_elf) { $desc.app_elf } else { $desc.project_name + '.elf' }
$elf = Join-Path $Repo ('build\' + $elfName)
$logDir = Join-Path $Repo 'logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
# Ten log = ma build nhu man About: fdictdm10 + 4 hex dau SHA256 cua file ELF (= esp_app_get_elf_sha256 tren may).
# Khong doc duoc ELF -> ten cu <project>_build<HHmm>.
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
# Doc SHA256 bang .NET (khong dung Get-FileHash: cua so mo tu pwsh/IDF co the thua huong PSModulePath sai -> lenh loi -> ten cu).
$buildId = $null; $hashErr = $null
try {
    $fs = [System.IO.File]::Open($elf, 'Open', 'Read', 'ReadWrite')
    try { $h = [System.Security.Cryptography.SHA256]::Create().ComputeHash($fs) } finally { $fs.Dispose() }
    $buildId = 'fdictdm10' + (-join ($h[0..1] | ForEach-Object { $_.ToString('x2') }))
} catch { $hashErr = $_.Exception.Message }
$log = if ($buildId) { Join-Path $logDir ('{0}_{1}.log' -f $stamp, $buildId) }
       else { Join-Path $logDir ('{0}_{1}_build{2}.log' -f $stamp, $desc.project_name, $bin.LastWriteTime.ToString('HHmm')) }
$utf8 = New-Object System.Text.UTF8Encoding($false)
function Write-Log([string]$s) { [System.IO.File]::AppendAllText($log, $s + "`r`n", $utf8) }

Write-Host "LOG: $log" -ForegroundColor Cyan
if ($hashErr) { Write-Host "Khong doc duoc ELF ($elf): $hashErr" -ForegroundColor Yellow }
Write-Log ("=== {0}  bin {1} ({2} B, {3})  port {4} ===" -f (Get-Date -Format 's'), $bin.Name, $bin.Length, $bin.LastWriteTime.ToString('s'), $Port)

if (-not $NoFlash) {
    Write-Log "=== idf.py -p $Port flash ==="
    $ErrorActionPreference = 'Continue'
    idf.py -p $Port flash 2>&1 | ForEach-Object { $line = "$_"; Write-Host $line; Write-Log $line }
    $rc = $LASTEXITCODE
    if ($rc -ne 0) {
        Write-Log "=== flash loi, exit $rc ==="
        Write-Host "Flash loi (exit $rc). Log: $log" -ForegroundColor Red
        exit $rc
    }
}

Write-Log "=== monitor $Port (Ctrl+] de thoat) ==="
$env:FD_MONITOR_LOG = $log
$py = if ($env:IDF_PYTHON_ENV_PATH) { Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe' } else { 'C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe' }
& $py (Join-Path $Repo 'tools\monitor_log.py') -p $Port -b 115200 --toolchain-prefix xtensa-esp32s3-elf- --target esp32s3 --save-log $elf
Write-Host "Monitor da dong. Log: $log" -ForegroundColor Cyan
