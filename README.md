# EDA_wxWidghets

## 登录与欢迎页

打开 `x64/Debug/wx_demo.exe`。联网使用时输入用户名和 DeepSeek API Key，点击“登录并继续”；不接入 API 时点击“无需 API，直接使用”，用户名留空则以访客进入。程序显示带用户名的欢迎页，自动准备 AI 助手服务，就绪后进入主工作区；也可点击“进入工作区”。准备失败时可返回登录修改信息。

这是本机工作台的会话入口，不创建远程账户。登录检查必填项与密钥格式；密钥有效性在实际使用 AI 助手时由 DeepSeek 返回。密钥默认隐藏，可切换显示，仅用于本次运行，不写入工程或配置文件。用户名显示在主窗口标题中。

需要安装 Python 3，并能运行 `py` 或 `python` 命令。程序自动在随机本地端口启动自己的 Python 服务，通过匿名管道交付密钥，使用临时令牌连接，退出程序时关闭该服务。日常使用无需单独启动下面的手动服务；编译后会把 `assistant_server.py`、`assistant_skills.py`、`assistant_winhttp.py`、`skills` 与 `res` 一起复制到输出目录。

## Verilog 代码工作区

通过主菜单 **Verilog → 代码编辑器**（Ctrl+Alt+V）打开独立代码窗口。支持 `.v`、`.sv`、`.vh`、`.svh` 多文件标签、Verilog/SystemVerilog 语法高亮、行号、代码折叠、空格缩进、括号匹配、Ctrl+Space 关键字补全，以及查找/替换、撤销/重做、UTF-8 文件打开和保存。新建、模板和画布生成的代码均有未保存标记；关闭文件或退出主程序时会询问保存。

**模板 → 全加器与 Testbench** 会创建设计和测试两个代码标签，顶层模块为 `full_adder_tb`。Testbench 遍历八组输入、检查结果并生成 `wave.vcd`。另有组合逻辑模块、DFF 和通用 Testbench 模板。

**Verilog → 从当前画布生成 Verilog** 将组合逻辑转换为一个新的 `circuit_top` 代码标签。支持 INPUT/OUTPUT、BUTTON、CONSTANT、LED/PROBE、六种基本逻辑门、MUX 和单比特 COMPARATOR。端口名按稳定元件 ID 生成为 `in_c<ID>` / `out_c<ID>`；未连接输入接常量 0。画布和代码分别保存。时钟、DFF 等时序元件需要在代码工作区中明确编写时钟与复位逻辑，暂不自动转换。

编辑、模板和文件操作无需 API 或外部编译器。**F9 编译检查**和 **Ctrl+F9 运行仿真**使用本机 Icarus Verilog 的 `iverilog.exe` / `vvp.exe`；程序会检测 PATH、常见安装目录及编译器相邻目录，也可在“工具设置”中选择路径。未安装工具时会在输出区域提示，编辑和保存继续可用。

本地依赖位于 `tools/iverilog`，原始包保留在 `downloads`；已部署到 `x64/Debug/tools/iverilog`，当前程序可自动识别，无需手动配置或修改 PATH。后续编译自动复制完整工具目录。换电脑或重新下载源码时，运行 `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\setup-verilog.ps1` 恢复；保留下载包后也可加 `-Offline` 离线恢复。版本、校验值和说明见 `tools/README.md`。附带 GTKWave 可用于打开另存的 VCD。

构建默认使用 SystemVerilog 2012、全部打开的非空 `.v/.sv` 文件，也可选择仅当前文件；已打开的 `.vh/.svh` 会作为头文件快照。编译读取未保存的编辑缓冲区，在独立临时目录运行，保留相邻源目录的相对包含关系，并允许从原目录读取未打开的包含文件。顶层模块可以填写，也可留空自动选择。错误与警告双击定位代码行；代码后续更改会让旧定位失效。停止按钮和单阶段超时会终止本次创建的进程组。仿真生成的 VCD 可以通过“另存波形”保留，下一次构建或关闭工作区后临时产物会清理。

检查入口：`tests/run_regression.ps1 -WithoutDebugSymbols -VerilogIntegration`；加 `-RealVerilog` 使用本地真实 Icarus 验证全加器、DFF、画布生成代码、头文件包含及诊断定位。测试覆盖原生编辑控件高亮、中文路径、UTF-8 保存、替换/撤销、画布生成、未保存头文件、异步构建与诊断、VCD、超时和取消。外部工具调用使用测试替身，不等同于真实 Icarus 的 HDL 语义验证。

真实工具检查入口：`tests/run_regression.ps1 -WithoutDebugSymbols -RealVerilog`。使用本地 Icarus/VVP 实际编译并仿真全加器、DFF、画布导出的 XOR 和相对头文件，并检查 VCD 与真实报错定位。

### 本地 Verilog 依赖

已配置 `tools/iverilog` 便携工具目录，包含 Icarus Verilog 12.0 Windows 发行包、VVP、必需 DLL/后端及 GTKWave 3.3.100。下载来自 [Windows 发行维护页](https://bleyer.org/icarus/)，解包工具来自 [innoextract](https://constexpr.org/innoextract/)。`tools/verilog-dependencies.json` 固定下载地址和 SHA-256。原始下载包保留在本机 `downloads`。

新电脑克隆后，在仓库根目录运行 `powershell -NoProfile -ExecutionPolicy Bypass -File tools/setup-verilog.ps1` 下载、校验并解包；已有下载缓存可加 `-Offline`。无需管理员权限，也不修改系统 PATH。构建时自动将整个工具目录复制到程序旁的 `tools/iverilog`，编辑器自动识别。GTKWave 位于 `tools/iverilog/gtkwave/bin/gtkwave.exe`，可以打开另存的 VCD。下载缓存和解包后的二进制目录不提交 Git，由脚本恢复。

## 无需 API 直接使用

登录页点击“无需 API，直接使用”，会经过欢迎页进入主工作区。绘图、保存、导入导出、仿真和教程等本地功能直接可用，不需要填写密钥或启动 Python 助手服务。主窗口标题显示“离线模式”。

打开 **帮助 → AI 助手** 或桌宠的助手入口，即可使用“离线工具箱”。它使用程序内置规则和模板，支持新建半加器、全加器及六种基本逻辑门，整理当前电路布局、标注输入输出、检查连接结构、旋转选中元件和恢复自动走线。可从下拉框选择操作后直接执行，也可输入这些指令。“给步骤”只解释，“直接完成”应用修改；先询问某个电路的步骤，再说“直接新建”也能继续该主题。

默认追加新电路；明确输入“清空并新建全加器”等指令才替换原有内容。每次修改都通过本地校验，可用一次 Ctrl+Z 撤销。整理布局保留元件 ID、连接和参数，恢复自动走线，并移动邻近标注。取消或等待期间的手动修改会阻止旧操作应用。未识别的指令只显示支持范围，不会自动切换到 API。

离线工具箱不是本地大语言模型，不提供任意开放式问答。在已连接 DeepSeek 的会话中，可通过“模式”下拉框切换联网助手与离线工具箱；从离线入口进入后，如需联网助手，请重启并在登录页填写密钥。离线功能的实现位于 `wx_demo/OfflineAssistant.cpp`，不调用 API、不上传画布，也不启动 Python。

运行检查：`tests/run_regression.ps1 -WithoutDebugSymbols -OfflineIntegration`，覆盖无 Python PATH、无用户名和密钥的访客启动，半加器/全加器真值表、现有电路整理、标注、追问、撤销、取消及过期操作保护。

## 根据当前画布编辑的 AI skill

在 AI 助手的 DeepSeek 联网模式中输入需求并点击“直接完成”。每次请求会读取最新元件 ID、位置、旋转、真实引脚、电平、连线、文本及当前选择；默认同时发送由绘图区绘制函数生成的 PNG。图像仅包含画布，可关闭“附带画布图像”；元件、连线与文本数据仍会发送给 DeepSeek。默认 `deepseek-flash` 支持图像，配置其它模型时保留结构化上下文。

助手支持移动、旋转、增删现有元件，修改已有标注，删除或重接导线、调整折线路径，也可以新建电路。对话保留最近历史，因此“直接新建”“把刚才的输入移到左边”等追问会带上先前需求。“新对话”清除聊天上下文，不清空电路。

勾选“自动改进”后执行“读取画布 → 生成操作 → 本地校验和执行 → 重新读取画布和图像 → 检查或修正”，每个需求最多三次模型请求。失败的批次不改画布，错误会反馈给助手；等待期间手动编辑会停止应用旧方案。连续自动修改合并为一个 Ctrl+Z 撤销步骤，取消请求保留已经执行的修改，也可整体撤销。

实际加载的规则位于 `skills/circuit-editor/SKILL.md`，编辑工具参数位于 `skills/circuit-editor/tools.json`，加载器为 `assistant_skills.py`。服务每次请求重新读取 skill 文件；编译时将其复制到程序旁。修改源码目录中的 skill 后重新编译，或直接修改正在运行的服务旁的 skill 文件。工具只操作本应用的电路文档，不执行模型生成的程序或系统命令。

Windows 上的 DeepSeek HTTPS 请求使用系统 WinHTTP / Schannel，证书由 Windows 校验，避开当前环境中 Python OpenSSL 握手被中断的问题；非 Windows 环境保留 urllib。仅启用 TLS 1.2 / 1.3，不关闭证书校验、不跟随重定向，密钥仍只发送到固定的 DeepSeek HTTPS 地址。网络自动模式在每次模型请求前用相同通道进行无密钥探测，当前路径失败后尝试另一条代理/直连路径；只发送一次模型 POST，避免网络超时后重复计费。`DEEPSEEK_PROXY=system/direct/指定 HTTP 代理` 仍可显式固定连接方式。诊断命令为 `py assistant_server.py --check-network`。

检查入口：`py -X utf8 -B -m unittest discover -s tests -p "test_assistant*.py"`、`tests/run_storage_regression.ps1 -WithoutDebugSymbols`、`tests/run_regression.ps1 -WithoutDebugSymbols -SkillIntegration`。最后一项运行原生客户端和真实本地服务，只模拟 DeepSeek 响应，检查现有元件修改、PNG 变化、失败修正、对话历史、撤销、取消和过期结果保护，不使用真实密钥或付费调用。

## AI 悬浮插件

启动程序会显示 DS 鲸鱼娘 AI 悬浮插件。透明、无边框的角色直接叠在电路绘图区上，拖动角色可以改变位置，活动范围限制在本窗口的绘图区。透明处仍可操作画布；**视图 → AI 悬浮插件** 控制显示，**F1** 保留原快捷键说明。

渲染、拖动、摸头、尾巴点击、动作、表情、气泡和右键控制面板复用 `dsh-pet-live2d` 的原始客户端。角色旁的 AI 入口打开原有 AI 助手；处理、完成、失败和取消会通知原互动控制器。桌宠本身不调用 DeepSeek；提交 AI 需求仍使用原有本地助手服务。

### 其他电脑的桌宠部署

完整发布目录必须保留程序旁的 `res/pet`、`tools/setup-pet.ps1`、`repair-pet.cmd` 和 `start-wx-demo.cmd`。源码中的 x64 `WebView2Loader.dll` 已纳入 Git；`.gitignore` 显式放行桌宠资源目录中的 x64 加载器，同时继续忽略构建输出。构建前后都会运行 `tools/check-pet.ps1` 检查模型引用、贴图、动作及两种位数的加载器。

新电脑先运行 `start-wx-demo.cmd`。它检查当前用户的 Cubism Core 和 WebView2 Runtime，缺少时从 Live2D / Microsoft 官方地址取得依赖，再打开程序。也可在应用中选择 **帮助 → 修复桌宠依赖**，修复成功后自动重新加载。过程日志在 `%LOCALAPPDATA%/wx_demo/pet-setup.log`。只有缺少 WebView2 Runtime 时才启动微软安装器，默认随当前用户权限安装。

Cubism Core 不提交到 Git；安装脚本从官方托管地址下载到当前用户缓存，也支持 `-CoreFile` 指定自行取得的官方 SDK 文件。程序还支持 `wx_demo.exe` 旁 `runtime/live2d/live2dcubismcore.min.js` 的便携布局。无网络时先准备 Core 文件，并在 `downloads` 放入微软官方 x64 独立安装器 `MicrosoftEdgeWebView2RuntimeInstallerX64.exe`，运行 `repair-pet.cmd -Offline -CoreFile "实际文件路径"`。小型 WebView2 bootstrapper 仍需要网络，不能代替离线安装器。

GitHub 的源代码 ZIP 不含编译出的 exe。向普通用户发布时应提供完整的 Release 程序目录；不要只发 exe，也不要使用依赖开发环境的 Debug 程序。脚本和资源无需硬编码开发者的账户或桌面路径。开发者可运行 `tools/check-pet.ps1 -CheckGit` 检查必需资源是否已纳入 Git。

插件使用 WebView2 Composition 与 DirectComposition 进行透明合成，不依赖 DSH 或独立服务器。资源位于 `wx_demo/res/pet`，编译后随 `res` 目录复制到程序旁。模型、贴图和 React 运行库均使用本地资源；Cubism Core 不随项目分发，优先读取 `%LOCALAPPDATA%/wx_demo/live2d/live2dcubismcore.min.js`，否则尝试 Live2D 官方运行时。启动阶段与错误记录位于 `%LOCALAPPDATA%/wx_demo/pet-plugin.log`，不记录问题或 API Key。

运行库地址由原生宿主在文档创建前注入：本机 Core 缓存和静态 vendor 文件直接通过对应虚拟主机读取。它们不依赖 `WebResourceRequested` 拦截；WebView2 不会为虚拟主机映射的资源触发该事件。

Visual Studio 构建结束后会检查生成的 EXE，自动移除它从输出目录继承的 Low 完整性标签。只处理生成的程序文件，避免程序启动后无法写缓存或创建透明合成层。若旧日志不可写，诊断会静默转到 `%TEMP%/wx-demo-pet-进程号.log`；失败状态也会带上具体阶段和错误码。

保留上善无形、ZipZipPipe、氵六青的模型署名及 CC BY-NC-SA 4.0 许可，原适配代码 A8Chann / MIT；完整声明见 `wx_demo/res/pet/licenses`。原模型美术和 DSH 源码保持不变，通过薄适配层对接 wx_demo。

资源检查入口：`py -m unittest discover -s tests -p test_pet_assets.py`；`tests/run_regression.ps1 -WithoutDebugSymbols -PetIntegration` 为可选运行检查入口。

## DeepSeek 电路助手

正常使用时，在登录页填写 [DeepSeek 开放平台](https://platform.deepseek.com/) 创建的 API Key 即可。在 **帮助 → AI 助手** 中输入需求。服务使用 Python 标准库，无需安装 Python 包。

开发调试仍支持手动启动独立的 `127.0.0.1:8765` 服务；应用的登录会话使用自己的端口，不会占用或停止这项独立服务。在 **PowerShell 7** 中切到本项目根目录：

```powershell
$env:DEEPSEEK_API_KEY = Read-Host -MaskInput 'DeepSeek API Key'
py .\assistant_server.py
```

AI 助手支持：

- **给步骤**：只返回文字。例如“请说明如何用异或门和与门搭半加器”。
- **直接完成**：例如“帮我搭一个双输入与门电路”。助手可使用当前可用元件创建电路，也能修改现有元件、连线和标注。本地检查通过后才整批应用，按一次 **Ctrl+Z** 可撤销同一需求的自动改进。未要求清空时保留现有内容；等待期间若改过画布，旧结果不会应用。

模型默认 `deepseek-flash`，需要时在启动服务前设置 `DEEPSEEK_MODEL`。服务通过 DeepSeek 的 `/responses` 接口发送需求、最近对话和当前画布上下文；其它本地文件不会发送。退出时可在服务终端按 **Ctrl+C**。

若提示连接失败，在启动服务的同一个 PowerShell 窗口按 **Ctrl+C**，运行 `py .\assistant_server.py --check-network`。它不读取 API Key，也不会调用模型；收到 **HTTP 401** 表示该连接路径可达。默认启动时优先探测 Python 的系统/环境代理设置，连接失败时再探测直连。

如果代理探测失败而直连可达，可在该窗口执行以下命令，再回到助手重试：

```powershell
$env:DEEPSEEK_PROXY = 'direct'
py .\assistant_server.py
```

`DEEPSEEK_PROXY` 也可设为 `auto`（默认）、`system`（使用系统/环境设置），或支持 HTTPS CONNECT 的 HTTP 代理地址，如 `http://127.0.0.1:7890`。指定 HTTP 代理时，Python 仍遵守 `NO_PROXY`。这些设置只影响本地助手服务，不改 Windows 的网络配置。

服务测试：`py -m unittest discover -s tests -p test_assistant_server.py`；画布改图、校验和撤销测试包含在原存储回归中。

## 新手教程与导出

通过 **帮助 → 新手教程**（Ctrl+T）选择半加器或全加器的手动教程，也可选择对应的**自动演示**。自动演示从空画布开始，每隔约 2.4 秒放置一个元件或添加一条连线；连线会绕开元件本体。说明框显示当前动作与进度；可暂停、继续或结束，完成后保留电路供查看和编辑。开始前会询问是否保存当前修改。输入、输出标签跟随元件移动；打开 AI 助手或整批替换电路时结束当前教程，避免继续引用旧元件。

文件菜单保留 KiCad PCB 导入，并提供两个独立入口：“导出 KiCad 原理图”和“导出 KiCad 网表 (.net)”。原理图导出目前支持输入、输出和双输入 XOR、AND、OR；直接网表导出面向一位全加器，生成带芯片封装、库引脚及网络节点的 KiCad S-expression 网表，不需要调用 `kicad-cli`。原理图入口只保存原理图，不再自动启动网表转换。遇到不支持的电路会提示；无效输入不会替换原目标文件。

`tests/run_regression.ps1 -WithoutDebugSymbols` 同时验证 AI、教程与导出菜单共存、工具复位、连线通知、标签刷新、中文导出路径及失败时原文件保留。

## KiCad PCB 导入与联动（2026-11-05）

通过“文件 → 从 KiCad 导入 PCB…”或 Ctrl+O 打开 `.kicad_pcb`，在独立 PCB 工作区查看封装、焊盘、走线/圆弧、过孔、板框和网络。支持图层筛选、网络/元件搜索、网络高亮、缩放平移、原样另存快照，以及“在 KiCad 中打开”和保存后自动刷新。PCB 工作区用于导入查看和检查，板图编辑仍在 KiCad 中完成，现有数字原理图与仿真功能继续保留。

可导入 [KiCad 原生示例板](examples/kicad_import_demo.kicad_pcb)；操作、格式范围和验证记录见 [KiCad 联动说明](KiCad联动说明.md)。

## 数字时序仿真（2026-11-05）

已完成 CLOCK、理想 DFF（仅 D、CLK、Q、QN 四个引脚，上升沿采样）、PROBE，以及 BUTTON、CONSTANT、数字指示灯 LED。下方新增仿真控制与数字波形：运行/暂停/继续/停止/复位、下一事件、指定时钟周期步进、时间缩放与滚动、A/B 游标、CSV/VCD 导出。初值、时钟参数、探针名称及运行限制保存为 XML v3，运行电平不污染设计和撤销记录。已有三输入 RESET 和四输入 SET/CLR 的 DFF 文件仍按原有引脚语义读取；新放置的 DFF 只有四个信号引脚。

启动 `x64/Debug/wx_demo.exe`，点击“分频示例”即可体验；也可打开 [二分频示例](examples/dff_divider.xml) 或 [两级 DFF 示例](examples/dff_chain.xml)。双击时钟/触发器/探针编辑参数。**F5 复位、F6 切换输入、F7 下一事件、F8 运行/继续。** 完整操作、时间语义、文件兼容和测试说明见 [时序仿真说明](时序仿真说明.md)。本轮为二态零延迟数字版本，ngspice 模拟电路后端仍属后续阶段。

### 自定义输入波形（2026-11-24）

仿真面板的“编辑输入波形”可为 INPUT / BUTTON 画出自己的激励：在波形区点击添加边沿、拖动边沿调整时间、双击输入精确时间、右键删除；也可用“生成方波”“生成脉冲”按周期、占空比或脉宽批量生成。时间单位支持自动 / ns / us / ms / s，编辑时可把停止时间直接设为 ms 或 s 级；波形图刻度会随跨度自动切换单位，不再固定显示 ns。波形作为元件属性写入 XML v3（`waveform="0:0,100:1,200:0"`），随撤销、保存、复位一起生效，旧文件不受影响。

## 结构重构

元件创建已统一到注册表，求解改为独立快照，并拆出设计文档、无向网络与共同数字拓扑规则。新建画布使用 XML v3，继续使用稳定命名引脚；旧 v1/v2 文件仍可打开并按原版本保存，添加时序内容后升级为 v3；下文 XML 示例保留 v1 写法。基础重构记录见 [结构重构说明](结构重构说明.md)，时序扩展见上述最新说明。

## 旋转、连线位置与菜单

- 输入、输出端口在工具栏和元件库中统一使用与画布一致的方框图标，连接点分别在右侧、左侧。
- 使用选择工具直接拖动导线的线段，可改变横线的高度或竖线的左右位置；端点保持连接，首尾线段会自动增加折点。选中的导线显示线段手柄，Esc 或右键取消此次拖动，“连线 → 恢复自动走线”恢复默认路径。
- 选中逻辑门或输入、输出端口后，按 R 顺时针旋转 90°，Shift+R 逆时针旋转；也可使用“元件”菜单。引脚、选择框和连接线路同步转动，端口数值保持正向显示。复制、保存、重开和撤销保留角度与手动连线路径。
- 菜单分为文件、编辑、视图、元件、连线、仿真、帮助。视图可切换网格与吸附，并分别显示或隐藏元件库、属性栏；F5 复位仿真，F6 切换选中输入的电平，F7 下一事件，F8 运行/继续，F1 查看快捷键。

## XML 电路文件

“文件”菜单支持新建（Ctrl+N）、打开（Ctrl+O）、保存（Ctrl+S）和另存为（Ctrl+Shift+S）。首次保存时选择 `.xml` 路径，后续保存写回同一文件。标题中的 `*` 表示有未保存的修改；新建、打开或退出时会询问是否保存，取消则继续编辑。

文件采用 UTF-8 XML，保存全部已实现元件的类型、位置、旋转角度、输入引脚数、输入端口的 0/1 状态、引脚连接与手动路径，以及文字内容、位置、大小和字号。打开后重新计算电路电平，未手动修改的导线自动布线。旧版 XML 仍可打开，省略角度时按 0°、省略路径时自动布线。支持中文路径、中文及多行文字。此格式是本项目的电路格式，不直接兼容其他 EDA 软件的项目文件。

`wx_demo/CircuitDocument.h` 定义文档数据，`CircuitFile.h/.cpp` 负责 XML 读写；`CanvasDocument.cpp` 负责画布与文档之间的转换。下面是 `version="1"` 的兼容格式示例，元件通过正整数 ID 关联，`sourcePin`、`targetPin` 从 0 开始：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<circuit version="1">
  <components>
    <component id="1" type="INPUT" x="100" y="100" inputCount="0" high="1"/>
    <component id="2" type="OUTPUT" x="300" y="100" inputCount="1" high="0"/>
  </components>
  <wires>
    <wire sourceId="1" sourcePin="0" targetId="2" targetPin="0"/>
  </wires>
  <texts>
    <text x="100" y="200" width="240" height="60" pointSize="14">示例电路</text>
  </texts>
</circuit>
```

读取会校验格式版本、元件类型、ID、参数、引脚、重复连接、多驱动和反馈环路。文件错误时保留当前画布；保存先写临时文件，成功后再替换目标，避免写入中断损坏原文件。当前文件大小上限 8 MiB，支持最多 1000 个元件、4000 条连线和 1000 个文字框；坐标范围 ±1000000，文字框宽高 1～1000000，字号 6～96，单框最多 65536 字符、所有文字合计最多 262144 字符。

文件功能回归测试：`tests/run_storage_regression.ps1`。如本机调试符号工具异常，可加 `-WithoutDebugSymbols`。

## 编辑快捷键

- 删除：Ctrl+D，删除当前选中的元件、导线或文字框；编辑文字时删除选中的文字。Delete / Backspace 仍可用于删除画布选中对象。
- 撤销：Ctrl+Z，恢复上一项画布修改；编辑文字时撤销当前这次文字编辑中的输入。结束文字编辑后，可在画布上撤销这一段文字修改。

画布保留最近 50 步修改，一次拖动算一步；撤销删除元件会同时恢复关联导线。保存后仍可撤销，新建或打开文件会清空撤销历史。

## 连线与逻辑仿真

1. 放置输入端口、逻辑门和输出端口。当前支持 AND、OR、NOT、NAND、NOR、XOR 六种门电路。
2. 选择工具栏或左侧树中的“连线 / 导线”工具，依次点击红色输出引脚和蓝色输入引脚。也可以先点输入，再点输出。输入、输出端口的连接点直接位于方框边缘，不再伸出短导线；可点击整个方框连接。
3. 起点选中后会显示跟随鼠标的虚线预览；点击终点完成连接。按 Esc、右键或切换工具可取消未完成的连线。
4. 一个输出可以连接多个输入；同一个输入只接受一条导线。程序会阻止输出接输出、输入接输入、重复连接和组合逻辑反馈环路。
5. 切回“选择”工具，单击输入端口的方框切换 0/1。电路自动计算并逐级传播，输出端口显示结果；绿色导线代表 1，灰色导线代表 0，选中的导线显示蓝色。
6. 单击导线可查看来源、目标和当前值。按 Ctrl+D 或 Delete / Backspace 删除选中的导线，或用删除工具点击线路。
7. 拖动元件时，导线自动跟随引脚。删除元件会移除其关联线路；减少输入引脚数量时，会移除连接到被删除引脚的线路。

未连接的输入默认为 0。线路采用自动直角走线，交叉处不会自动电气连接；分支通过同一输出分别连接多个输入实现。复制单个元件时保留其引脚数量，但不会复制外部导线。

新增模块：`Circuit.h / Circuit.cpp` 管理连接、校验和逻辑传播；`Wire.h / Wire.cpp` 管理线路路径、绘制和点击判定；`CanvasWiring.cpp` 接入画布交互。

## 门电路参数与操作

- 与门、或门、与非门、或非门、异或门默认有 **2 个输入、1 个输出**。
- 选择门电路工具，在左侧属性栏修改“输入引脚数”（**2～16**），再点击画布放置。
- 也可以选中已放置的门电路，直接修改“输入引脚数”。门的高度会自动适配；点击判定、选择框和拖动刷新范围同步调整。
- 非门固定为 **1 个输入、1 个输出**。蓝点代表输入，红点代表输出。
- 修改数量会保留仍然存在的输入引脚电平，新增引脚初始为低电平；减少数量会移除末尾引脚。
- 复制粘贴保留元件的输入数量和输入端口的 0/1 设置；逻辑门的输入电平由连线决定。多输入异或门按“高电平输入数量为奇数时输出高电平”计算。
- 代码中也可以通过构造参数设置数量，例如 `AndGate gate(5);`，或调用 `gate.SetInputCount(3);`。超出范围的构造参数会抛出异常，`SetInputCount` 则返回 `false` 并保持原状。

回归验证：在安装了项目所需 Visual Studio 和 wxWidgets 的环境中运行 `tests/run_regression.ps1`。覆盖门电路真值表、参数调整、端口框边连接点与框内点击、端口连线拖动残影、绘制与点击范围、拖动和缩放后的像素一致性、复制粘贴、属性控件、全部引脚连接、分支、多级逻辑传播、断线及环路校验。若本机调试符号工具存在版本问题，可使用 `tests/run_regression.ps1 -WithoutDebugSymbols` 完成无调试符号的验证。

## 目前已经完成:

基本窗口

toolbox(树状结构)和canvas的分区

toolbar和menubar模块

toolbox树状结构分区

仿真电路逻辑后端

引脚连线、自动直角走线及电平传播

六种门电路的前后端连接和组合逻辑计算

拖拽与或非门和输入输出端口

输入文字

XML 电路文件的保存、加载和另存为

## 需要完成:



后续元件（时钟、存储、复用器等）的放置和仿真


**res文件夹要放在x64编译的debug文件夹内,和.exe文件同目录**

































































































































































































