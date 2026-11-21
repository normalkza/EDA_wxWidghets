param([string]$RepoRoot = 'C:\Users\28370\Desktop\wx_demo', [string]$BuildName = 'pet')
$ErrorActionPreference = 'Stop'
$buildDir = Join-Path $RepoRoot "tests\build\$BuildName"
$entrySource = 'tests\pet_regression.cpp'
$testTarget = 'pet_regression'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
[xml]$project = Get-Content -LiteralPath (Join-Path $RepoRoot 'wx_demo.vcxproj') -Raw
$namespace = $project.DocumentElement.NamespaceURI
$manager = [System.Xml.XmlNamespaceManager]::new($project.NameTable)
$manager.AddNamespace('m', $namespace)
foreach ($item in @($project.SelectNodes('//m:ItemGroup/m:ClCompile | //m:ItemGroup/m:ClInclude | //m:ItemGroup/m:Image', $manager))) {
    $item.ParentNode.RemoveChild($item) | Out-Null
}
$sources = $project.CreateElement('ItemGroup', $namespace)
$assistantSources = @('wx_demo\PetWebView.cpp', 'wx_demo\AiApiClient.cpp', 'wx_demo\AiAssistantDialog.cpp', 'wx_demo\AiCircuitExecutor.cpp',
    'wx_demo\TutorialManager.cpp', 'wx_demo\TutorialOverlay.cpp', 'wx_demo\GedaNetlistExporter.cpp',
    'wx_demo\KiCadNetlistExporter.cpp', 'wx_demo\KiCadSchematicExporter.cpp')
foreach ($sourcePath in ($assistantSources + @('wx_demo\KiCadSExpr.cpp', 'wx_demo\KiCadPcbImporter.cpp', 'wx_demo\PcbCanvas.cpp', 'wx_demo\PcbFrame.cpp', 'wx_demo\MainFrame.cpp', 'wx_demo\MenuBar.cpp', 'wx_demo\WaveformPanel.cpp', 'wx_demo\SimulationPanel.cpp', 'wx_demo\DigitalSimulator.cpp', 'wx_demo\TraceRecorder.cpp', 'wx_demo\CanvasTiming.cpp', 'wx_demo\ComponentRegistry.cpp', 'wx_demo\TopologyRules.cpp', 'wx_demo\Connectivity.cpp', 'wx_demo\SimulationSnapshot.cpp', 'wx_demo\Canvas.cpp', 'wx_demo\CanvasWiring.cpp', 'wx_demo\CanvasDocument.cpp', 'wx_demo\Circuit.cpp', 'wx_demo\CircuitValidation.cpp', 'wx_demo\CircuitFile.cpp', 'wx_demo\Wire.cpp', 'wx_demo\PropertyPanel.cpp', 'wx_demo\SelectionState.cpp', 'wx_demo\Toolbox.cpp', 'wx_demo\ToolBar.cpp', $entrySource))) {
    $item = $project.CreateElement('ClCompile', $namespace)
    $item.SetAttribute('Include', (Join-Path $RepoRoot $sourcePath))
    $sources.AppendChild($item) | Out-Null
}
$project.DocumentElement.AppendChild($sources) | Out-Null
foreach ($includes in $project.SelectNodes('//m:ClCompile/m:AdditionalIncludeDirectories', $manager)) {
    $includes.InnerText = (Join-Path $RepoRoot 'wx_demo') + ';' + $includes.InnerText
}
foreach ($subsystem in $project.SelectNodes('//m:Link/m:SubSystem', $manager)) {
    $subsystem.InnerText = 'Console'
}
$testProject = Join-Path $buildDir ($testTarget + '.vcxproj')
$project.Save($testProject)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuildPath = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (!$msbuildPath) { throw 'MSBuild not found.' }
Write-Host "MSBuild: $msbuildPath"
$launcher = Join-Path $RepoRoot 'tests\_clean_launch.exe'
& $launcher --exe $msbuildPath --cwd $RepoRoot $testProject /t:Build /p:Configuration=Debug /p:Platform=x64 /p:PostBuildEventUseInBuild=false "/p:TargetName=$testTarget" "/p:OutDir=$buildDir\bin\" "/p:IntDir=$buildDir\obj\" /m /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'pet regression build failed.' }
New-Item -ItemType Directory -Path (Join-Path $buildDir 'bin\res') -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'wx_demo\res') | Copy-Item -Destination (Join-Path $buildDir 'bin\res') -Recurse -Force
& (Join-Path $buildDir "bin\$testTarget.exe")
Write-Host "exit=$LASTEXITCODE"
