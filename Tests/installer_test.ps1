# GKDIST - 24-band multiband distortion plug-in
# Copyright (C) 2026 Roman Gutekunst (GK)
# SPDX-License-Identifier: GPL-3.0-or-later

# End-to-end test of the installer (non-elevated test build, temporary folders).
param([string]$Root = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'

$setup = Join-Path $Root 'build-installer\GKDISTSetupTest_artefacts\Release\GKDIST Setup.exe'
$out   = Join-Path $Root 'build\GKDIST_artefacts\Release'
$base  = Join-Path $Root 'build\installer-test'
if (Test-Path $base) { Remove-Item -Recurse -Force $base }
New-Item -ItemType Directory -Force $base | Out-Null

$vst3 = Join-Path $base 'VST3'
$app  = Join-Path $base 'Program Files\GKDIST'
$menu = Join-Path $base 'Start Menu\GKDIST'
$common = @("/vst3dir=$vst3", "/appdir=$app", "/menudir=$menu", '/noregistry')
$script:fail = 0

function Check($cond, $msg) { if ($cond) { Write-Host "  ok: $msg" } else { Write-Host "  FAIL: $msg"; $script:fail++ } }
function Quote($list) { $list | ForEach-Object { '"' + $_ + '"' } }
function RunSetup($exe, $extra) {
    $p = Start-Process -FilePath $exe -ArgumentList (Quote (@('/S') + $common + $extra)) -Wait -PassThru
    return $p.ExitCode
}
function Hash($f) { if (Test-Path -LiteralPath $f) { (Get-FileHash -LiteralPath $f).Hash } else { 'missing' } }

$dll = "GKDIST.vst3\Contents\x86_64-win\GKDIST.vst3"
$pairs = @(
    @("$out\VST3\$dll", "$vst3\$dll"),
    @("$out\VST3\GKDIST.vst3\Contents\Resources\moduleinfo.json", "$vst3\GKDIST.vst3\Contents\Resources\moduleinfo.json"),
    @("$out\Standalone\GKDIST.exe", "$app\GKDIST.exe"))

Write-Host "[1] Silent install"
$code = RunSetup $setup @("/log=$base\install.log")
Check ($code -eq 0) "exit code $code"
Get-Content "$base\install.log" | ForEach-Object { Write-Host "      $_" }
foreach ($p in $pairs) { Check ((Hash $p[0]) -eq (Hash $p[1])) ("byte-identical: " + $p[1].Substring($base.Length + 1)) }
Check (Test-Path "$app\Uninstall GKDIST.exe") "uninstaller installed"
Check (Test-Path "$app\Third-Party Notices.txt") "third-party notices installed"
Check (Test-Path "$menu\GKDIST.lnk") "Start Menu shortcut: GKDIST"
Check (Test-Path "$menu\Uninstall GKDIST.lnk") "Start Menu shortcut: Uninstall"

Write-Host "[2] Reinstall while the plug-in is loaded by another process (like a running DAW)"
$holderScript = Join-Path $base 'holder.ps1'
Set-Content -LiteralPath $holderScript -Encoding UTF8 -Value @'
param([string]$Path)
Add-Type -Name K -Namespace W -MemberDefinition '[DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr LoadLibrary(string p);'
[W.K]::LoadLibrary($Path) | Out-Null
Start-Sleep -Seconds 30
'@
$holder = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', ('"' + $holderScript + '"'), '-Path', ('"' + "$vst3\$dll" + '"'))
$loaded = $false
for ($i = 0; $i -lt 40 -and -not $loaded; $i++) {
    Start-Sleep -Milliseconds 250
    try { [IO.File]::Open("$vst3\$dll", 'Open', 'ReadWrite', 'None').Close() } catch { $loaded = $true }
}
Check $loaded "plug-in DLL is locked by the holder process"
$code = RunSetup $setup @("/log=$base\reinstall.log")
Check ($code -eq 0) "exit code $code"
Check ((Select-String -Path "$base\reinstall.log" -Pattern 'moved aside' -Quiet)) "in-use binary was moved aside instead of failing"
Check ((Hash "$out\VST3\$dll") -eq (Hash "$vst3\$dll")) "new plug-in binary in place"
Stop-Process -Id $holder.Id -Force
$holder.WaitForExit()

Write-Host "[3] Silent uninstall with the installed uninstaller"
$un = "$app\Uninstall GKDIST.exe"
$code = RunSetup $un @("/log=$base\uninstall.log")
Check ($code -eq 0) "exit code $code"
Get-Content "$base\uninstall.log" | ForEach-Object { Write-Host "      $_" }
for ($i = 0; $i -lt 40 -and (Test-Path $app); $i++) { Start-Sleep -Milliseconds 250 }   # self-delete runs after exit
Check (-not (Test-Path "$vst3\GKDIST.vst3")) "VST3 bundle removed"
Check (-not (Test-Path $menu)) "Start Menu folder removed"
Check (-not (Test-Path $un)) "uninstaller removed itself"
Check (-not (Test-Path $app)) "program folder removed"

if ($script:fail -eq 0) { Write-Host "`nINSTALLER TEST PASSED"; exit 0 } else { Write-Host "`nINSTALLER TEST FAILED ($($script:fail))"; exit 1 }
