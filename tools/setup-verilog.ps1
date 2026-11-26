param([switch]$Offline)
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$repoRoot = Split-Path -Parent $PSScriptRoot
$downloads = Join-Path $repoRoot 'downloads'
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'verilog-dependencies.json') -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Path $downloads -Force | Out-Null
function Get-Package($package) {
    $path = Join-Path $downloads $package.file
    if (!(Test-Path -LiteralPath $path)) {
        if ($Offline) { throw "Missing offline package: $path" }
        $partial = $path + '.partial'
        Invoke-WebRequest -UseBasicParsing -Uri $package.url -OutFile $partial -TimeoutSec 180
        if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $package.sha256) { throw "Checksum mismatch: $partial" }
        Move-Item -LiteralPath $partial -Destination $path -Force
    }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $package.sha256) { throw "Checksum mismatch: $path" }
    return $path
}
$installer = Get-Package $manifest.icarus
$extractor = Get-Package $manifest.extractor
$toolRoot = Join-Path $PSScriptRoot 'iverilog'
if (!(Test-Path -LiteralPath $toolRoot)) {
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    $stage = Join-Path $tempRoot ('wx-verilog-setup-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage | Out-Null
    try {
        Expand-Archive -LiteralPath $extractor -DestinationPath (Join-Path $stage 'unpacker')
        & (Join-Path $stage 'unpacker\innoextract.exe') --extract --silent --output-dir (Join-Path $stage 'payload') $installer
        if ($LASTEXITCODE -ne 0) { throw 'Cannot extract Icarus package.' }
        $payload = [IO.Path]::GetFullPath((Join-Path $stage 'payload\app'))
        foreach ($required in @('bin\iverilog.exe','bin\vvp.exe','lib\ivl\ivl.exe','gtkwave\bin\gtkwave.exe')) {
            if (!(Test-Path -LiteralPath (Join-Path $payload $required))) { throw "Package is missing $required" }
        }
        if (!$payload.StartsWith($stage + '\', [StringComparison]::OrdinalIgnoreCase) -or
            !([IO.Path]::GetFullPath($toolRoot)).StartsWith([IO.Path]::GetFullPath($PSScriptRoot) + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid extraction destination.' }
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'licenses\Icarus-GPL-2.0.txt') -Destination (Join-Path $payload 'COPYING.txt')
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'verilog-dependencies.json') -Destination (Join-Path $payload 'wx-demo-dependencies.json')
        Move-Item -LiteralPath $payload -Destination $toolRoot
    } finally {
        $resolved = [IO.Path]::GetFullPath($stage)
        if (!$resolved.StartsWith($tempRoot + '\wx-verilog-setup-', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe temporary cleanup path.' }
        if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
    }
}
foreach ($required in @('bin\iverilog.exe','bin\vvp.exe','lib\ivl\ivl.exe','gtkwave\bin\gtkwave.exe')) {
    if (!(Test-Path -LiteralPath (Join-Path $toolRoot $required))) { throw "Incomplete local tools: missing $required" }
}
function Show-ToolVersion([string]$executable) {
    $savedPreference = $ErrorActionPreference
    try {
        # VVP prints its normal version message to stderr. Windows PowerShell
        # 5.1 wraps that stream as ErrorRecord even when the exit code is zero.
        $ErrorActionPreference = 'Continue'
        $version = @(& $executable -V 2>&1)
        $toolExitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    if ($toolExitCode -ne 0) { throw "Tool version check failed: $executable" }
    Write-Output ([string]($version | Select-Object -First 1))
}
Show-ToolVersion (Join-Path $toolRoot 'bin\iverilog.exe')
Show-ToolVersion (Join-Path $toolRoot 'bin\vvp.exe')
foreach ($relativeOutput in @('x64\Debug','x64\Release','Debug','Release')) {
    $output = Join-Path $repoRoot $relativeOutput
    if (Test-Path -LiteralPath (Join-Path $output 'wx_demo.exe')) {
        & (Join-Path $PSScriptRoot 'deploy-verilog.ps1') -OutputDirectory $output
    }
}
Write-Output 'Setup complete. Open Verilog -> code editor; no system PATH changes are needed.'
