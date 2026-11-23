# Run from the reviewed queue checkout, as the Windows bench operator.
param(
    [ValidateSet('main', 'staging')][string]$Track = 'main',
    [string]$HomeDirectory = 'C:\rad-hil',
    [switch]$EnableAdmission
)
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$worker = Join-Path $HomeDirectory 'release-worker'
$receipt = Join-Path $HomeDirectory 'release-admission'
$harness = Join-Path $HomeDirectory 'harness'
foreach ($name in @('run.py', 'ossm_guard.py', 'catalog.py')) {
    if (!(Test-Path -LiteralPath (Join-Path $harness $name))) { throw 'Install the reviewed base hardware harness first.' }
}
if (Test-Path -LiteralPath (Join-Path $worker 'current.json')) { throw 'Finish or reconcile the active queue job before changing this worker.' }
$taskName = 'RAD Firmware Validation Queue'
$existing = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($existing -and $existing.State -eq 'Running') { throw 'Stop the idle queue worker before upgrading it.' }
New-Item -ItemType Directory -Force -Path $worker,$receipt | Out-Null
$owner = '*' + $identity.User.Value + ':(OI)(CI)F'
foreach ($directory in @($worker, $receipt)) {
    & icacls $directory /inheritance:r /grant:r $owner '*S-1-5-32-544:(OI)(CI)F' '*S-1-5-18:(OI)(CI)F' /Q
    if ($LASTEXITCODE -ne 0) { throw 'Unable to protect the worker files.' }
}
& icacls $receipt /grant:r '*S-1-5-20:(OI)(CI)RX' /Q
if ($LASTEXITCODE -ne 0) { throw 'Unable to grant the GitHub runner read access to admission receipts.' }
$tokenPath = Join-Path $worker 'queue-token.dpapi'
if (!(Test-Path -LiteralPath $tokenPath)) {
    $secret = Read-Host -Prompt 'Scoped firmware rig API token' -AsSecureString
    if ($secret.Length -lt 1) { throw 'A rig API token is required.' }
    ConvertFrom-SecureString $secret | Set-Content -LiteralPath $tokenPath -Encoding ASCII
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'release_admission.py') -Destination $receipt -Force
$launcher = Join-Path $worker 'start-release-queue.ps1'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'start-release-queue.ps1') -Destination $launcher -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'release_queue.py') -Destination $harness -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'release_health.py') -Destination $harness -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'launch.py') -Destination $harness -Force
# Windows client defaults to the Restricted execution policy, which makes -File exit 1
# before the launcher runs. Scope the bypass to this reviewed launcher only.
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument ('-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "{0}" -HomeDirectory "{1}" -Track {2}' -f $launcher,$HomeDirectory,$Track)
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $identity.Name
$principal = New-ScheduledTaskPrincipal -UserId $identity.Name -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 10 -RestartInterval (New-TimeSpan -Minutes 1) -MultipleInstances IgnoreNew
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null
Start-ScheduledTask -TaskName $taskName
if ($EnableAdmission) {
    New-Item -ItemType File -Force -Path (Join-Path $receipt 'enabled') | Out-Null
}
