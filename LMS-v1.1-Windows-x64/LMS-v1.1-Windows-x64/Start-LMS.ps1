$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$demoPort = 8080
if (Test-Path -LiteralPath 'server.port') {
    $savedPort = (Get-Content -LiteralPath 'server.port' -Raw).Trim()
    if (-not [int]::TryParse($savedPort, [ref]$demoPort) -or $demoPort -lt 1 -or $demoPort -gt 65535) {
        throw 'server.port must contain a port number between 1 and 65535.'
    }
}
$serverUrl = "http://127.0.0.1:$demoPort"
Set-Content -LiteralPath 'server.url' -Value $serverUrl -Encoding Ascii
function Test-DemoServer {
    $connection = New-Object System.Net.Sockets.TcpClient
    try {
        $pending = $connection.ConnectAsync('127.0.0.1', $demoPort)
        if ($pending.Wait(500)) { return $connection.Connected }
        return $false
    } catch { return $false } finally { $connection.Dispose() }
}
if (-not (Test-Path -LiteralPath 'lms.db')) {
    Write-Host 'First-time setup: create the administrator password.'
    & '.\lms_server.exe' --init-admin admin
    if ($LASTEXITCODE -ne 0) { throw 'Administrator setup failed.' }
}
if (-not (Test-DemoServer)) {
    $server = Start-Process -FilePath (Join-Path $PSScriptRoot 'lms_server.exe') -ArgumentList '--host','127.0.0.1','--port',"$demoPort" -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $PSScriptRoot 'server.log') -RedirectStandardError (Join-Path $PSScriptRoot 'server-errors.log') -PassThru
    Set-Content -LiteralPath 'server.pid' -Value $server.Id
    $ready = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        if (Test-DemoServer) { $ready = $true; break }
        if ($server.HasExited) { break }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ready) { throw 'The server could not start. See server-errors.log in this folder.' }
}
Write-Host "Connecting to $serverUrl"
if (Test-Path -LiteralPath 'School-LMS.exe') {
    Start-Process -FilePath (Join-Path $PSScriptRoot 'School-LMS.exe') -WorkingDirectory $PSScriptRoot -WindowStyle Normal
    exit 0
}
if (Test-Path -LiteralPath 'lms_gui.exe') {
    Start-Process -FilePath (Join-Path $PSScriptRoot 'lms_gui.exe') -WorkingDirectory $PSScriptRoot -WindowStyle Normal
    exit 0
}
& '.\lms_client.exe' $serverUrl
if ($LASTEXITCODE -ne 0) { throw 'The client exited with an error.' }
