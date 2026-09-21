[CmdletBinding()]
param(
    [string]$ConsoleIp = '127.0.0.1',
    [ValidateRange(1,65535)][int]$Port = 3232,
    [ValidateRange(5,180)][int]$Seconds = 60
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force "$root/artifacts" | Out-Null
$path = "$root/artifacts/klog-$(Get-Date -Format 'yyyyMMdd-HHmmss').log"
$client = [Net.Sockets.TcpClient]::new()
$file = $null
try {
    $connect = $client.ConnectAsync($ConsoleIp, $Port)
    if (!$connect.Wait(4000) -or !$client.Connected) { throw 'klogsrv unavailable' }
    $stream = $client.GetStream()
    $stream.ReadTimeout = 500
    $file = [IO.FileStream]::new($path, [IO.FileMode]::CreateNew,
        [IO.FileAccess]::Write, [IO.FileShare]::Read)
    $buffer = [byte[]]::new(16384)
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $total = 0
    Write-Host "Klog capture connected for $Seconds seconds: $path"
    while ($timer.Elapsed.TotalSeconds -lt $Seconds -and $total -lt 16777216) {
        try { $read = $stream.Read($buffer, 0, $buffer.Length) }
        catch [IO.IOException] {
            if ($_.Exception.InnerException -is [Net.Sockets.SocketException] -and
                $_.Exception.InnerException.SocketErrorCode -eq [Net.Sockets.SocketError]::TimedOut) { continue }
            throw
        }
        if ($read -eq 0) { break }
        $file.Write($buffer, 0, $read)
        $file.Flush()
        $total += $read
    }
    Write-Host "Captured $total bytes. Log: $path"
    if ($total -ge 16777216) { Write-Warning 'Capture stopped at the 16 MiB limit' }
} finally {
    if ($file) { $file.Dispose() }
    $client.Dispose()
}

