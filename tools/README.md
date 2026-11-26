# Verilog 本地依赖

已配套 Icarus Verilog 12.0 (devel)、同版本 VVP、运行所需 DLL/后端文件，以及包内 GTKWave 3.3.100。Windows 包版本固定为 `2022-06-11 x64`，可满足当前编辑器使用的 `-g2012`、`-grelative-include`、`-t null` 与 VCD 工作流。下载来源为 Windows 打包维护者 https://bleyer.org/icarus/ ，版本和 SHA-256 记录在 `verilog-dependencies.json`。

目录：

- `tools/iverilog/`：完整本地工具，包括 `bin`、`lib/ivl`、`include` 和 `gtkwave`，需要整体保留。
- `downloads/`：Icarus 原始安装包和 innoextract 压缩包，均保留，便于离线恢复。
- `x64/Debug/tools/iverilog/`：当前运行目录的副本，编辑器自动检测其中的 `bin/iverilog.exe`、`bin/vvp.exe`。
- `tools/licenses/Icarus-GPL-2.0.txt`：Icarus 上游许可文本；来源为源码提交 `2693dd32b` 的 `COPYING`。innoextract 的许可随其压缩包保留。

在工程根目录恢复依赖并部署到已有程序输出目录：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\setup-verilog.ps1
```

只使用已下载的包，不联网：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\setup-verilog.ps1 -Offline
```

脚本核验固定 SHA-256，通过 innoextract 解包，不运行安装向导，不更改系统 PATH。后续 Visual Studio / MSBuild 编译会通过 `build/VerilogTools.targets` 自动增量复制工具到输出目录。`downloads/` 和第三方工具目录已加入 Git 忽略，仓库保留恢复脚本、清单和许可。

运行真实工具的集成检查：

```powershell
$env:WXWIN = 'D:\wxWidgets-3.3.3'
.\tests\run_regression.ps1 -WithoutDebugSymbols -RealVerilog
```

覆盖内置全加器八组输入、真实 VCD、画布生成的 XOR、DFF 复位与采样、未保存相对头文件、真实编译报错定位；同时运行编辑器专项检查。

使用入口：主菜单 **Verilog → 代码编辑器 → 模板 → 全加器与 Testbench**，F9 检查、Ctrl+F9 仿真，再用“另存波形”保留 VCD。查看波形可运行 `tools/iverilog/gtkwave/bin/gtkwave.exe`，通过 File → Open New Tab 打开保存的 VCD。也可从命令行直接传入 VCD 路径。GTKWave 是外部查看器，尚未嵌入主窗口。
