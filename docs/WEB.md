# Web 平台支持与交付规范

GDPP 的 Web 目标使用 Godot 官方 GDExtension 动态链接模型：桌面 Godot 编辑器内运行
`compiler` 插件，项目脚本先转译为 C++，再由 Emscripten 生成 wasm32 side module。最终网页由
Godot 官方启用 GDExtension 的 Web 导出模板加载该模块；浏览器中不包含 GDPP 编译器、SDK、
godot-cpp 静态库、生成 C++ 或项目 `.gd`。

## 支持范围

| 能力 | 当前状态 |
|---|---|
| Godot 4.4、4.5、4.6、4.7 Web SDK 结构 | ✅ 自动构建与 CI 矩阵 |
| wasm32 Release/debug side module | ✅ |
| 单线程 `nothreads` | ✅ 独立 SDK、缓存、文件名和描述符 |
| Web Worker `threads` | ✅ 独立 SDK、缓存、文件名和描述符 |
| 官方 dlink Web 模板选择 | ✅ 导出预设强制启用 `variant/extensions_support` |
| `.gd`、compiler、SDK 和原生中间物剥离 | ✅ PCK/完整目录内容门禁 |
| Wasm 入口、dylink、内存线程属性检查 | ✅ |
| Chromium 启动与 AOT 行为 oracle | ✅ 已建立 Linux CI 阻断门禁；本机缺少 Godot Web 模板，待流水线首次留档 |
| Safari、Firefox、移动浏览器和真实 CDN | 尚未认证 |
| SIMD、WebGPU、PWA 离线缓存和自定义内存增长策略 | 尚未认证 |

这里的“支持”指构建与导出链路已实现，不代表所有浏览器组合均已商业认证。对外发布前仍应以
目标浏览器、真实服务器响应头、客户资源规模和长时间运行结果为准。

## 用户安装与导出

用户需要安装三部分：当前桌面平台的 GDPP 主插件、目标 Godot 小版本对应的 Web target pack，
以及 Emscripten。target pack 叠加到项目后应形成：

```text
addons/gdpp/sdk/<godot-version>/web/wasm32/
├── nothreads/
│   ├── godot-cpp/
│   ├── include/
│   ├── lib/
│   ├── src/
│   └── sdk.manifest
└── threads/
    ├── godot-cpp/
    ├── include/
    ├── lib/
    ├── src/
    └── sdk.manifest
```

项目设置 `gdpp/build/emscripten_cxx` 默认是 `em++`，可改为绝对路径。用户不需要 CMake、
Ninja、Python 或 SCons；这些只用于 GDPP 自身生成 target pack，导出时插件直接执行 `em++`。

Godot Web 导出预设必须满足：

- [x] `Variant > Extensions Support` 已启用；否则没有动态链接主模块，GDPP 会阻断导出。
- [x] 线程模式与安装的 target pack 一致；插件根据 Godot 导出 feature 精确选择
  `threads` 或 `nothreads`，不允许静默混用对象缓存。
- [x] `gdpp/strip_gdscript_sources=true` 且
  `gdpp/allow_source_fallback=false`；商业预设不能在失败时悄悄交付脚本。
- [x] 已安装 Godot 对应的 Web dlink 导出模板。

示例项目提供 `Web AOT`、`Web AOT Threads` 和 `Web GDScript Fallback` 三个预设。前两者用于
二进制交付，后者只用于行为与体积对照。

## 线程与服务器约束

两种模式不是同一个 ABI：

| 模式 | GDPP 文件名 | Wasm 内存 | 适用场景 |
|---|---|---|---|
| `nothreads` | `libgdpp.release.web.wasm32.nothreads.wasm` | 普通导入内存 | 默认兼容路线、较简单部署 |
| `threads` | `libgdpp.release.web.wasm32.threads.wasm` | `shared` 导入内存 | 明确需要 Web Worker 并完成隔离部署 |

线程版必须由服务器返回 COOP/COEP 响应头，iframe 的父页面也必须满足隔离条件。GDPP 的测试
服务器 `tools/serve_web.py` 固定返回：

```text
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
Cross-Origin-Resource-Policy: cross-origin
Cache-Control: no-store
```

该服务器仅用于本地和 CI，不应替代正式 CDN/Web 服务器配置。即使选择无线程模式，使用
GDExtension 的官方 Web 模板仍有额外兼容和安全约束，不能直接双击 HTML 运行。

## 构建和缓存隔离

Web 原生缓存位于：

```text
addons/gdpp/build/project/native-direct/
└── <godot>/web/wasm32/<nothreads|threads>/<debug|release>/
```

`build-configuration.txt` 同时记录原生构建修订、Godot API、平台、架构、profile、Emscripten
路径和线程模式。任何一项变化都会使旧对象失效。导出期反射使用 compiler 提供的 metadata-only
脚本描述，不先构建或加载桌面宿主项目库；一次 Web 导出只交叉编译当前线程模式和
debug/release profile 的一个 Wasm 模块，编辑器不会尝试加载 Wasm。

项目编译和 target pack 都启用源路径映射。Release Wasm 不应出现客户工程或构建机的绝对路径；
SDK manifest 的 `source_paths mapped` 是强制契约，旧 target pack 会被安全拒绝。

## 维护者构建 target pack

从仓库根目录执行：

```sh
cmake --preset dev \
  -DGDPP_WEB_SDK_VERSIONS=4.5 \
  -DGDPP_WEB_VARIANTS='nothreads;threads' \
  -DGDPP_WEB_EMCMAKE="$(command -v emcmake)"
cmake --build --preset dev --target gdpp_web_sdk --parallel
```

Godot 4.4 target pack 使用 Emscripten 3.1.62 系列；Godot 4.5 及以上使用 4.0.0+。构建脚本会
拒绝明显不匹配的版本，并把全部 CMake/Ninja 中间物限制在根目录 `build/`。生成的 target pack
只进入 `example/addons/gdpp/sdk/`，发布工作流按 Godot 版本和线程模式分别归档，避免用户下载
不需要的全部平台矩阵。

## 自动化验收

`.github/workflows/web.yml` 分成两个独立职责：

- [x] 4 个 Godot API 版本 × 2 个线程模式构建 SDK，验证 manifest、库数量、命名和线程隔离。
- [x] Godot 4.5.2 × 2 个线程模式执行 compiler 插件构建、Release Web 导出、PCK 内容审计、
  Wasm 验证、`dylink.0`/入口/共享内存检查和 Chromium 运行 oracle。
- [x] HTTP 门禁检查 COOP/COEP；AOT 运行时通过 `JavaScriptBridge` 写入 DOM
  oracle，无依赖 Chrome `--dump-dom` 必须读到 `data-gdpp-status="ok"`，并拒绝
  `LinkError`、`RuntimeError`、`CompileError` 和脚本错误。
- [x] Web 工作流为每个版本和线程模式生成独立 target pack；它们不混入标准三宿主 × 四 Godot
  SDK 的 12 个桌面/移动插件包，也不会把 Emscripten 或 godot-cpp 源码带进游戏成品。

本地 2026-07-17 使用 Emscripten 5.0.7 和 Godot 4.5 API SDK 的可复现结果：10 个示例脚本
生成 12 个项目/运行时编译单元，单线程和线程版均由 NativeBuilder 完整编译、链接并通过
Binaryen 全 feature 验证。`nothreads`/`threads` Release 文件分别为 1,015,600 B
与 1,026,366 B，debug 文件分别为 3,176,015 B 与 3,217,253 B；均包含
`dylink.0` 和 `gdpp_project_library_init`。线程版导入 `(memory ... shared)`，单线程版
不含 shared 内存。target pack 分别为 87.99 MiB 和 90.57 MiB，其静态库及项目
Wasm 都通过构建机/客户绝对路径泄漏审计。
静态库时间戳变化的增量验收仅生成 1 条重链命令，不再重编译 12 个单元。
Web Debug 与 Release 都使用 `-O3`、section GC 和裁剪后的 `template_release` 绑定；Debug
仅额外保留 GDPP 脚本断言，不携带系统库 DWARF 绝对路径。
该轮没有本地 Godot Web 导出模板和可自动化浏览器，因此不能把 CI 门禁“已实现”写成“已在
本机浏览器通过”；流水线首次运行结果应补入平台实测报告。
