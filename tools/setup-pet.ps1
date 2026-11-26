param(
    [string]$ExecutableDirectory = '',
    [string]$CoreFile = '',
    [string]$DataDirectory = '',
    [switch]$Offline,
    [switch]$DownloadOnly,
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$packageRoot = Split-Path -Parent $PSScriptRoot
if (!$ExecutableDirectory) { $ExecutableDirectory = Join-Path $packageRoot 'x64\Debug' }
$ExecutableDirectory = [IO.Path]::GetFullPath($ExecutableDirectory)
if (!$DataDirectory) { $DataDirectory = Join-Path $env:LOCALAPPDATA 'wx_demo' }
$DataDirectory = [IO.Path]::GetFullPath($DataDirectory)
$downloads = Join-Path $packageRoot 'downloads'
New-Item -ItemType Directory -Path $DataDirectory -Force | Out-Null
$transcript = $false
function Test-Core([string]$path) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { return $false }
    $file = Get-Item -LiteralPath $path
    if ($file.Length -lt 10000 -or $file.Length -gt 4MB) { return $false }
    return ([IO.File]::ReadAllText($path).Contains('Live2DCubismCore'))
}
function Get-WebViewVersion {
    $client = '{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}'
    foreach ($hive in @([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryHive]::CurrentUser)) {
        foreach ($view in @([Microsoft.Win32.RegistryView]::Registry32, [Microsoft.Win32.RegistryView]::Registry64)) {
            $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey($hive, $view)
            try {
                $key = $base.OpenSubKey("Software\Microsoft\EdgeUpdate\Clients\$client")
                if ($key) {
                    try { $version = [string]$key.GetValue('pv') } finally { $key.Dispose() }
                    if ($version -and [version]$version -gt [version]'0.0.0.0') { return $version }
                }
            } finally { $base.Dispose() }
        }
    }
    return ''
}
try {
    Start-Transcript -LiteralPath (Join-Path $DataDirectory 'pet-setup.log') -Append | Out-Null
    $transcript = $true
    $portable = Join-Path $ExecutableDirectory 'runtime\live2d\live2dcubismcore.min.js'
    $cached = Join-Path $DataDirectory 'live2d\live2dcubismcore.min.js'
    if (!(Test-Core $portable) -and !(Test-Core $cached)) {
        if ($CheckOnly) { throw 'Cubism Core is missing. Run repair-pet.cmd, or provide -CoreFile from the official SDK.' }
        New-Item -ItemType Directory -Path (Split-Path -Parent $cached) -Force | Out-Null
        $partial = $cached + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
        try {
            if ($CoreFile) {
                if (!(Test-Core $CoreFile)) { throw 'The provided Cubism Core file is invalid.' }
                Copy-Item -LiteralPath $CoreFile -Destination $partial
            } elseif ($Offline) { throw 'Offline setup needs an existing Core cache or -CoreFile.' }
            else {
                Invoke-WebRequest -UseBasicParsing -Uri 'https://cubism.live2d.com/sdk-web/cubismcore/live2dcubismcore.min.js' -OutFile $partial -TimeoutSec 45
            }
            if (!(Test-Core $partial)) { throw 'The official Core download is invalid or incomplete.' }
            Move-Item -LiteralPath $partial -Destination $cached -Force
        } finally { if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Force } }
    }
    Write-Output 'Cubism Core is ready (portable runtime or current-user cache).'
    $version = Get-WebViewVersion
    if (!$version -or $DownloadOnly) {
        if ($CheckOnly) { throw 'WebView2 Runtime is not installed. Run repair-pet.cmd.' }
        New-Item -ItemType Directory -Path $downloads -Force | Out-Null
        $installer = Join-Path $downloads 'MicrosoftEdgeWebview2Setup.exe'
        $standalone = Join-Path $downloads 'MicrosoftEdgeWebView2RuntimeInstallerX64.exe'
        if (Test-Path -LiteralPath $standalone) { $installer = $standalone }
        if (!(Test-Path -LiteralPath $installer)) {
            if ($Offline) { throw 'Missing offline WebView2 installer. Supply MicrosoftEdgeWebView2RuntimeInstallerX64.exe in downloads.' }
            $partial = $installer + '.partial'
            Invoke-WebRequest -UseBasicParsing -Uri 'https://go.microsoft.com/fwlink/p/?LinkId=2124703' -OutFile $partial -TimeoutSec 60
            Move-Item -LiteralPath $partial -Destination $installer -Force
        }
        $signature = Get-AuthenticodeSignature -LiteralPath $installer
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
            throw 'The WebView2 installer does not have a valid Microsoft signature.'
        }
        if (!$version -and !$DownloadOnly) {
            if ($Offline -and $installer -ne $standalone) { throw 'The bootstrapper needs internet; offline installation requires the standalone installer.' }
            # Run a newly written TEMP copy to avoid an inherited Low integrity
            # label on downloaded files in a sandbox-labelled desktop folder.
            $stagedInstaller = Join-Path ([IO.Path]::GetTempPath()) ('wx-pet-webview2-' + [Guid]::NewGuid().ToString('N') + '.exe')
            $inputFile = [IO.File]::OpenRead($installer)
            try {
                $outputFile = [IO.File]::Create($stagedInstaller)
                try { $inputFile.CopyTo($outputFile); $outputFile.Flush($true) } finally { $outputFile.Dispose() }
            } finally { $inputFile.Dispose() }
            try {
                $process = Start-Process -FilePath $stagedInstaller -ArgumentList '/silent','/install' -WindowStyle Hidden -PassThru
                if (!$process.WaitForExit(240000)) { throw 'WebView2 installation is still running; wait, then run repair-pet.cmd again.' }
            } finally { Remove-Item -LiteralPath $stagedInstaller -Force -ErrorAction SilentlyContinue }
            $version = Get-WebViewVersion
            if (!$version) { throw "WebView2 installation did not complete. Exit code: $($process.ExitCode)" }
        }
    }
    if ($version) { Write-Output "WebView2 Runtime: $version" }
    if ($DownloadOnly) { Write-Output 'Official bootstrapper downloaded; no runtime installation requested.' }
    Write-Output 'Pet dependencies are ready. Reload the companion from the application.'
} catch {
    Write-Output ('PET SETUP FAILED: ' + $_.Exception.Message)
    exit 1
} finally { if ($transcript) { Stop-Transcript | Out-Null } }
