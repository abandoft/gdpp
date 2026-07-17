# GitHub Actions 流水线职责

本目录只保存 GitHub Actions 编排文件。Godot 安装复用
`.github/actions/setup-godot/`，编译、PCK 审计和运行差分继续由 CMake 与 `tools/` 实现，保证
CI 与本地使用同一套构建逻辑。

## 总体模型

`release.yml` 是唯一 PR 总门禁和正式发布入口。其余工作流同时支持 `workflow_call` 与手动
诊断，不再直接监听 PR，避免同一提交重复运行两套昂贵矩阵。

```text
PR 或手动发布
      │
      ├─ 版本、CHANGELOG 与不可变发布预检
      │
      ├─ Quality / Core / Native / Godot / Android / Web / iOS
      │                 全部通过
      │
      ├─ 12 组桌面宿主 SDK、Android 与 iOS 发布组件
      ├─ 12 个 ZIP 的合并、内容与 ABI 审计
      └─ Release readiness / All gates passed
                        │
                        └─ 仅手动 publish=true：创建并复核 Release
```

| 文件 | 唯一职责 | PR | 手动诊断 | 可复用 | 写权限 |
|---|---|---:|---:|---:|---:|
| `quality.yml` | C++ 格式、Actions 语义、权限、Action SHA、仓库与产物卫生 | 总门禁调用 | ✅ | ✅ | 无 |
| `core.yml` | AppleClang/GCC/MSVC 编译器核心构建与单元测试 | 总门禁调用 | ✅ | ✅ | 无 |
| `native-integration.yml` | 三桌面 compiler GDExtension、SDK、生成代码和直接原生构建 | 总门禁调用 | ✅ | ✅ | 无 |
| `godot-compatibility.yml` | Godot 4.4～4.7 导出/运行差分及固定官方项目语料 | 总门禁调用 | ✅ | ✅ | 无 |
| `android.yml` | Android arm64 target pack 与二进制 APK 构建/审计 | 总门禁调用 | ✅ | ✅ | 无 |
| `web.yml` | Web 双线程 target pack、Wasm/PCK 审计与 Chromium 运行 | 总门禁调用 | ✅ | ✅ | 无 |
| `ios.yml` | iOS 16 设备/模拟器 target pack、XCFramework 与 Xcode 构建 | 总门禁调用 | ✅ | ✅ | 无 |
| `release.yml` | 全量门禁、12 包构建、哈希、CHANGELOG 与不可变 GitHub Release | ✅ | ✅ | 否 | 仅 `publish` 作业 |

## PR 与分支保护

PR 总是展开完整测试和发布包矩阵，但永远不会创建标签或 Release。`main` 分支保护只需把稳定
汇总检查 `Release readiness / All gates passed` 设为 required；该检查直接依赖全部可复用测试、
20 个发布组件任务和 12 个 ZIP 任务，任何失败或跳过都会让汇总检查失败。

`godot-compatibility.yml` 仍可手动选择 `all`、单个精确 Godot 维护版本或
`official-corpus`，用于缩短故障诊断时间；总门禁固定传入 `all`。4.4 分支使用 4.4.1，插件
描述符和目标 ABI 最低版本仍为 4.4。

## 正式发布

正式发布只能从默认分支手动运行 `release.yml`，同时启用 `publish` 并提供不带 `v` 前缀、与
`plugin.cfg` 一致的 `release_version`。发布前后执行以下失败关闭检查：

- 版本符合 SemVer，`CHANGELOG.md` 对应正文存在且非空；
- 同名 tag 和 Release 均不存在，禁止静默覆盖正式版本；
- 发布提交仍是默认分支最新提交；
- 七类完整测试、12 个宿主/SDK 组合以及移动 target pack 全部成功；
- 12 个 ZIP 名称、数量、内容、ABI、最低系统与平台组合正确；
- Release 标题和 tag 等于纯版本号，包含 12 个 ZIP 与一个 `SHA256SUMS`；
- 新 tag 精确指向触发工作流的提交。

全部工作流顶层保持 `contents: read`，只有 `publish` 作业临时提升到 `contents: write`。所有外部
Action 固定到 40 位提交 SHA，注释保留对应主版本便于受控升级。中间制品限定在根 `build/` 或
`example/addons/gdpp/build/`，Actions artifact 使用有限保留期；正式资产由 GitHub Release
生命周期管理。

## 失败归属

- `Quality`：源码、仓库卫生、Action 固定或权限边界；
- `Core`：平台编译器或语言核心；
- `Plugin`：godot-cpp、compiler GDExtension、SDK 或直接原生构建；
- `Godot`：引擎版本加载、导出、运行、PCK 或性能差分；
- `Android`：NDK/SDK、ABI、APK 封装或内容；
- `Web`：Emscripten、线程模式、Wasm、PCK 或浏览器运行；
- `iOS`：Xcode/Apple SDK、切片、XCFramework 或模拟器构建；
- `Host/Target/ZIP`：正式包组件、平台契约或归档；
- `Release readiness`：任一上游测试或包未成功；
- `Publish`：版本、默认分支、不可变 tag、CHANGELOG、哈希或发布后资产契约。
