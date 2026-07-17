# GitHub Actions 流水线职责

本目录只保存 GitHub Actions 编排文件。可复用的 Godot 安装动作位于
`.github/actions/setup-godot/`；编译器、PCK 审计和运行差分逻辑继续由仓库中的 CMake 与
`tools/` 实现，避免 CI 成为另一套不可在本地复现的构建系统。

| 文件 | 唯一职责 | PR | 手动 | 自动发布 | 写权限 |
|---|---|---:|---:|---:|---:|
| `quality.yml` | 变更 C++ 格式、Actions 语义、权限边界、仓库与原生产物卫生 | ✅ | ✅ | 否 | 无 |
| `core.yml` | AppleClang/GCC/MSVC 编译器核心构建与单元测试 | ✅ | ✅ | 否 | 无 |
| `native-integration.yml` | 三桌面 compiler GDExtension、SDK、生成代码和直接原生构建 | ✅ | ✅ | 否 | 无 |
| `godot-compatibility.yml` | Godot 4.4～4.7 导出/运行差分及固定官方项目语料 | ✅ | ✅ | 否 | 无 |
| `android.yml` | Android arm64 目标 SDK 与二进制 APK 构建/内容审计 | ✅ | ✅ | 否 | 无 |
| `web.yml` | Web 双线程模式目标 SDK、Wasm/PCK 审计与 Chromium 运行验证 | ✅ | ✅ | 否 | 无 |
| `ios.yml` | iOS 16 真机/Universal Simulator target pack、XCFramework、Godot Xcode 工程与无签名 Simulator 构建 | ✅ | ✅ | 否 | 无 |
| `release.yml` | 三宿主 × 四 Godot SDK 的 12 个插件 ZIP、Android/iOS 合并、哈希、CHANGELOG 发布说明与受控 GitHub Release | ✅ | ✅ | 仅手动确认 | 仅 `publish` 作业 |

## 分支保护

`main` 应至少要求以下稳定检查名：

- `Quality / Source and repository`；
- 三个 `Core / <os>`；
- 三个 `Plugin / <os>`；
- `Godot / 4.4.1`、`Godot / 4.5.2`、`Godot / 4.6.3`、`Godot / 4.7`；
- `Official project corpus`；
- 四个 `Android SDK / Godot <version> / arm64`；
- `Android APK / Godot 4.5.2 / arm64`；
- 八个 `Web SDK / Godot <version> / <nothreads|threads>`；
- `Web browser / Godot 4.5.2 / nothreads` 与 `threads`。
- 四个 `iOS SDK / Godot <version> / arm64 + Universal Simulator`；
- `iOS Xcode project / Godot 4.6.2 / unsigned Simulator`。

手动运行 `godot-compatibility.yml` 时可选择 `all`、单个精确 Godot 维护版本或
`official-corpus`。PR 始终展开完整矩阵；定向重试只缩短故障诊断反馈时间，不能替代分支保护
要求的全量结果。4.4 分支固定使用最新维护版 4.4.1，插件描述符和目标 ABI 的最低版本仍为
4.4。

发布工作流不会替代上述合并门禁，但会在 PR 和手动运行中从干净 checkout 重新执行一次
Release 构建、Release 测试与打包。compiler GDExtension 固定使用 Godot 4.4 ABI；每个 ZIP 只带
一个版本的宿主 SDK 和同版本 Android arm64 target pack，mac-arm64 ZIP 再带 iOS arm64 target
pack。Debug 插件与原生集成由 `native-integration.yml` 独立验证。PR 永远不创建 GitHub
Release；手动运行只有同时启用 `publish` 并提供不带 `v` 前缀、且与 `plugin.cfg` 一致的
`release_version` 时才执行发布。Release 正文严格读取 `CHANGELOG.md` 对应 `## <version>` 标题
下的正文，缺失或为空直接失败。所有工作流顶层保持 `contents: read`，仅发布作业临时提升为
`contents: write`。工作流输出只能写入根 `build/` 或
示例项目约定的 `example/addons/gdpp/build/`，上传制品设置有限保留期；正式 Release 制品例外，
由 GitHub Release 生命周期管理。

## 失败归属

- `Quality` 失败：源码、仓库卫生或流水线权限问题；
- `Core` 失败：平台编译器或语言核心问题；
- `Plugin` 失败：godot-cpp、compiler GDExtension、SDK 或直接原生构建问题；
- `Godot` 失败：具体引擎版本的加载、导出、运行、PCK 或性能差分问题；
- `Android` 失败：NDK/SDK、Android ABI、APK 封装或包内容问题；
- `Web` 失败：Emscripten/SDK 线程模式、side-module Wasm、Web PCK 或浏览器运行问题；
- `iOS` 失败：Xcode/Apple SDK、真机/模拟器切片、XCFramework、Godot iOS 导出或无签名工程构建问题；
- `Release` 失败：12 包组合、最低系统契约、归档、版本/CHANGELOG 或发布权限问题。
