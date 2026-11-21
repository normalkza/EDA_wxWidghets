# WebView2 SDK

本目录的 `WebView2.h` 与 `wx_demo/res/pet/webview2/{x64,x86}/WebView2Loader.dll` 来自 Microsoft 官方 NuGet 包 [Microsoft.Web.WebView2 1.0.3485.44](https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.3485.44)。保留原 SDK 许可，运行时通过绝对路径加载对应架构的 Loader，不依赖修改全局 wxWidgets 安装。

Microsoft Edge WebView2 Runtime 由系统提供；插件使用 Composition Controller 与 DirectComposition 实现透明合成。Cubism Core 不包含在本工程中，用户运行时可从 Live2D 官方服务加载，或使用 `%LOCALAPPDATA%/wx_demo/live2d/live2dcubismcore.min.js` 本机缓存。
