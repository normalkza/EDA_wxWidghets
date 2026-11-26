param([string]$ResourceDirectory = '', [switch]$CheckGit)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (!$ResourceDirectory) { $ResourceDirectory = Join-Path $repoRoot 'wx_demo\res\pet' }
$ResourceDirectory = [IO.Path]::GetFullPath($ResourceDirectory).TrimEnd('\')
$files = @('index.html','pet.js','client.js','vendor.js','react.js','react-dom.js','pet.css','catalog.json',
    'model\c_0120.model3.json','webview2\x64\WebView2Loader.dll','webview2\x86\WebView2Loader.dll')
$modelPath = Join-Path $ResourceDirectory 'model\c_0120.model3.json'
if (!(Test-Path -LiteralPath $modelPath)) { throw "Missing model manifest: $modelPath" }
$references = (Get-Content -LiteralPath $modelPath -Raw -Encoding UTF8 | ConvertFrom-Json).FileReferences
$modelFiles = @($references.Moc) + @($references.Textures) + @($references.Physics) + @($references.Pose)
foreach ($expression in $references.Expressions) { $modelFiles += $expression.File }
foreach ($group in $references.Motions.PSObject.Properties) {
    foreach ($motion in $group.Value) { $modelFiles += $motion.File }
}
foreach ($file in $modelFiles) { if ($file) { $files += 'model\' + $file.Replace('/','\') } }
foreach ($relative in $files) {
    $path = [IO.Path]::GetFullPath((Join-Path $ResourceDirectory $relative))
    if (!$path.StartsWith($ResourceDirectory + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Invalid model reference: $relative" }
    if (!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) { throw "Missing pet resource: $relative" }
    if ($CheckGit) {
        $gitPath = $path.Substring([IO.Path]::GetFullPath($repoRoot).Length + 1).Replace('\','/')
        & git -C $repoRoot ls-files --error-unmatch -- $gitPath *> $null
        if ($LASTEXITCODE -ne 0) { throw "Pet resource is not tracked in Git: $gitPath" }
    }
}
foreach ($arch in @(@('x64',0x8664), @('x86',0x14c))) {
    $path = Join-Path $ResourceDirectory ("webview2\$($arch[0])\WebView2Loader.dll")
    $stream = [IO.File]::OpenRead($path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw 'Loader is not a PE binary.' }
        $stream.Position = 0x3c; $offset = $reader.ReadInt32()
        if ($offset -lt 0 -or $offset + 6 -gt $stream.Length) { throw 'Loader PE header is invalid.' }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne $arch[1]) { throw "Wrong loader architecture: $path" }
    } finally { $reader.Dispose() }
}
Write-Output "Pet package checked: $($files.Count) resources and both WebView2 loader architectures."
