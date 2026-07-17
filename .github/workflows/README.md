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
| `release.yml` | 可交付插件、Android/Web target pack、哈希和受控 GitHub Release | ✅ | ✅ | 仅手动确认 | 仅 `publish` 作业 |

## 分支保护

`main` 应至少要求以下稳定检查名：

- `Quality / Source and repository`；
- 三个 `Core / <os>`；
- 三个 `Plugin / <os>`；
- `Godot / 4.4`、`Godot / 4.5.2`、`Godot / 4.6.3`、`Godot / 4.7`；
- `Official project corpus`；
- 四个 `Android SDK / Godot <version> / arm64`；
- `Android APK / Godot 4.5.2 / arm64`；
- 八个 `Web SDK / Godot <version> / <nothreads|threads>`；
- `Web browser / Godot 4.5.2 / nothreads` 与 `threads`。

发布工作流不会替代上述合并门禁，但会在 PR 和手动运行中从干净 checkout 重新执行 Release
构建、测试与打包。PR 永远不创建 GitHub Release；手动运行只有同时启用 `publish` 并提供以
`v` 开头的 `release_tag` 时才执行发布。所有工作流顶层保持 `contents: read`，仅发布作业临时
提升为 `contents: write`。工作流输出只能写入根 `build/` 或示例项目约定的
`example/addons/gdpp/build/`，上传制品设置有限保留期；正式 Release 制品例外，由 GitHub
Release 生命周期管理。

## 失败归属

- `Quality` 失败：源码、仓库卫生或流水线权限问题；
- `Core` 失败：平台编译器或语言核心问题；
- `Plugin` 失败：godot-cpp、compiler GDExtension、SDK 或直接原生构建问题；
- `Godot` 失败：具体引擎版本的加载、导出、运行、PCK 或性能差分问题；
- `Android` 失败：NDK/SDK、Android ABI、APK 封装或包内容问题；
- `Web` 失败：Emscripten/SDK 线程模式、side-module Wasm、Web PCK 或浏览器运行问题；
- `Release` 失败：可交付包组合、符号、Universal 2、归档或发布权限问题。
