param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'iverilog'))
if (!(Test-Path -LiteralPath (Join-Path $sourceRoot 'bin\iverilog.exe'))) { return }
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
$targetRoot = [IO.Path]::GetFullPath((Join-Path $outputRoot 'tools\iverilog'))
if (!$targetRoot.StartsWith($outputRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $targetRoot.Equals($sourceRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid dependency destination.' }
$changedExecutables = @()
$copied = 0
foreach ($sourceFile in Get-ChildItem -LiteralPath $sourceRoot -Recurse -File) {
    $relative = $sourceFile.FullName.Substring($sourceRoot.Length + 1)
    $target = Join-Path $targetRoot $relative
    $existing = Get-Item -LiteralPath $target -ErrorAction SilentlyContinue
    if ($existing -and $existing.Length -eq $sourceFile.Length -and
        $existing.LastWriteTimeUtc -eq $sourceFile.LastWriteTimeUtc) { continue }
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $sourceFile.FullName -Destination $target -Force
    ++$copied
    if ($sourceFile.Extension -eq '.exe') { $changedExecutables += $target }
}
# New EXEs on this desktop can inherit Low integrity. Recreate only copied
# executables in normal TEMP and move them atomically, preserving timestamps.
if ($changedExecutables.Count -gt 0) {
    if (!('WxDemoVerilogFileMove' -as [type])) {
        Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class WxDemoVerilogFileMove {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool MoveFileEx(string source, string target, uint flags);
}
'@
    }
    $stage = Join-Path ([IO.Path]::GetTempPath()) ('wx-verilog-copy-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage | Out-Null
    try {
        foreach ($executable in $changedExecutables) {
            $full = [IO.Path]::GetFullPath($executable)
            if (!$full.StartsWith($targetRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Executable escaped dependency directory.' }
            $candidate = Join-Path $stage ([Guid]::NewGuid().ToString('N') + '.exe')
            if ([IO.Path]::GetPathRoot($candidate) -ne [IO.Path]::GetPathRoot($full)) { throw 'TEMP must be on the same volume as the output.' }
            $modified = [IO.File]::GetLastWriteTimeUtc($full)
            $inputFile = [IO.File]::OpenRead($full)
            try {
                $outputFile = [IO.File]::Create($candidate)
                try { $inputFile.CopyTo($outputFile); $outputFile.Flush($true) } finally { $outputFile.Dispose() }
            } finally { $inputFile.Dispose() }
            [IO.File]::SetLastWriteTimeUtc($candidate, $modified)
            if (![WxDemoVerilogFileMove]::MoveFileEx($candidate, $full, 9)) {
                throw [ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error())
            }
        }
    } finally {
        Get-ChildItem -LiteralPath $stage -File | Remove-Item -Force
        Remove-Item -LiteralPath $stage -Force
    }
}
Write-Output "Verilog tools ready: $targetRoot ($copied files updated)"
