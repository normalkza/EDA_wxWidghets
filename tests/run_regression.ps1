param([switch]$WithoutDebugSymbols)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
[xml]$project = Get-Content -LiteralPath (Join-Path $repoRoot 'wx_demo.vcxproj') -Raw
$namespace = $project.DocumentElement.NamespaceURI
$manager = [System.Xml.XmlNamespaceManager]::new($project.NameTable)
$manager.AddNamespace('m', $namespace)
foreach ($item in @($project.SelectNodes('//m:ItemGroup/m:ClCompile | //m:ItemGroup/m:ClInclude | //m:ItemGroup/m:Image', $manager))) {
    $item.ParentNode.RemoveChild($item) | Out-Null
}
$sources = $project.CreateElement('ItemGroup', $namespace)
foreach ($sourcePath in @('wx_demo\Canvas.cpp', 'wx_demo\CanvasWiring.cpp', 'wx_demo\Circuit.cpp', 'wx_demo\Wire.cpp', 'wx_demo\PropertyPanel.cpp', 'wx_demo\SelectionState.cpp', 'tests\gate_regression.cpp')) {
    $item = $project.CreateElement('ClCompile', $namespace)
    $item.SetAttribute('Include', (Join-Path $repoRoot $sourcePath))
    $sources.AppendChild($item) | Out-Null
}
$project.DocumentElement.AppendChild($sources) | Out-Null
foreach ($includes in $project.SelectNodes('//m:ClCompile/m:AdditionalIncludeDirectories', $manager)) {
    $includes.InnerText = (Join-Path $repoRoot 'wx_demo') + ';' + $includes.InnerText
}
foreach ($subsystem in $project.SelectNodes('//m:Link/m:SubSystem', $manager)) {
    $subsystem.InnerText = 'Console'
}
if ($WithoutDebugSymbols) {
    foreach ($info in $project.SelectNodes('//m:Link/m:GenerateDebugInformation', $manager)) { $info.InnerText = 'false' }
    foreach ($compile in $project.SelectNodes('//m:ItemDefinitionGroup/m:ClCompile', $manager)) {
        $info = $project.CreateElement('DebugInformationFormat', $namespace)
        $info.InnerText = 'OldStyle'
        $compile.AppendChild($info) | Out-Null
    }
    $settings = $project.CreateElement('PropertyGroup', $namespace)
    $incremental = $project.CreateElement('LinkIncremental', $namespace)
    $incremental.InnerText = 'false'
    $settings.AppendChild($incremental) | Out-Null
    $project.DocumentElement.AppendChild($settings) | Out-Null
}
$testProject = Join-Path $buildDir 'gate_regression.vcxproj'
$project.Save($testProject)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuildPath = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (!$msbuildPath) { throw '未找到 Visual Studio MSBuild。' }
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
Import-Module (Join-Path $vsInstall 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsInstall -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
& $msbuildPath $testProject /t:Build /p:Configuration=Debug /p:Platform=x64 /p:PostBuildEventUseInBuild=false /p:TargetName=gate_regression "/p:OutDir=$buildDir\bin\" "/p:IntDir=$buildDir\obj\" /m /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw '回归测试编译失败。' }
& (Join-Path $buildDir 'bin\gate_regression.exe')
if ($LASTEXITCODE -ne 0) { throw '回归测试未通过。' }
