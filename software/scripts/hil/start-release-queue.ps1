param(
    [ValidateSet('main', 'staging')][string]$Track = 'main',
    [string]$HomeDirectory = 'C:\rad-hil'
)
$ErrorActionPreference = 'Stop'
# The token is encrypted for the same Windows user as the local bench session.
$encrypted = Get-Content -Raw -LiteralPath (Join-Path $HomeDirectory 'release-worker\queue-token.dpapi')
$secret = ConvertTo-SecureString $encrypted.Trim()
$pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secret)
try {
    $env:FIRMWARE_VALIDATION_RIG_TOKEN = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
    & (Join-Path $HomeDirectory 'venv\Scripts\python.exe') -I -u (Join-Path $HomeDirectory 'harness\release_queue.py') --home $HomeDirectory --track $Track
    exit $LASTEXITCODE
} finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
    Remove-Item Env:FIRMWARE_VALIDATION_RIG_TOKEN -ErrorAction SilentlyContinue
}
