# GDPP

GDPP 是面向商业项目的 GDScript 原生编译器与 Godot GDExtension 编辑器插件。
编译器使用 C++17 全新实现，把受支持的 GDScript 转译为经过类型检查和优化的 GDExtension
C++，再调用用户已有的目标平台 C++ 工具链生成原生动态库。

## 产品边界

插件发行物只包含 `compiler` 编译器插件，不把编译服务带入最终游戏。用户项目会在本机生成
自己的原生库：内容寻址的 `development` 库用于开发期加载，稳定命名的 `debug` 或 `release`
库用于导出。导出插件会自动构建目标库，把场景和资源上的脚本实例
替换为内部原生类，验证产物后剥离项目 `.gd`、compiler 插件和 SDK。构建或替换失败时会保留
GDScript；二进制发布预设默认直接阻断，不会悄悄交付含源码的“成功”包。只有用户显式启用
`gdpp/allow_source_fallback`，或使用关闭源码剥离的普通 GDScript 预设时，才允许回退运行。
第三方二进制 GDExtension 本身由 Godot 原样加载，不需要再次 AOT；它所在 addon 的 GDScript
与项目其他脚本使用完全相同的 AOT 和源码剥离流程。动态调用和第三方单例不要求 C++ 头文件；
静态使用供应商类时，compiler 插件会从运行中的 ClassDB 自动采集 Runtime 契约，CLI 离线模式
可使用可审计的 `gdpp_bridge.json`；命名枚举和 bitfield 保留强类型、精确值及 Inspector
Enum/Flags 元数据。`extends` 第三方 GDExtension 类目前会严格阻断：Godot 能让
GDScript 附着到该类，但跨动态库 GDExtension 原生继承尚未实现，供应商 C++ SDK 本身不能解决。

正式用户不需要安装 CMake、Python、Ninja、SCons 或单独下载 godot-cpp，只需要：

- 当前已认证的 Godot 4.4～4.7 官方标准精度构建；后续稳定版在最新版语法、API/SDK 和回归
  矩阵完成后加入支持范围；
- 目标平台 C++ 工具链：Windows 使用 MSVC Build Tools，macOS 使用 Xcode Command Line
  Tools，Linux 使用发行版提供的 GCC/Clang 开发工具链；
- Android arm64-v8a 已支持自动交叉构建与 APK 导出，用户需 Android NDK；Web wasm32 已支持
  Emscripten side module、单/多线程隔离构建与 Web 导出，用户需 Emscripten 和匹配的 Web
  target pack；iOS 尚未完成，不能视为正式支持。

插件自身以 Godot 4.4 API 为最低公共基线。单一编译器内置 4.4、4.5、4.6 与 4.7 API 数据，默认按当前
编辑器选择目标版本，也可通过 `gdpp/target_godot_version` 显式选择。项目原生库使用对应
版本 SDK，生成描述符会写入准确的最低版本；自定义引擎或 double precision 构建仍需要匹配
的自定义 SDK。

语言前端不按 Godot 小版本维护多套官方语法，始终跟进项目声明支持的最新版稳定 GDScript；
4.4～4.7 仅是运行 API、SDK 和 ABI 目标。GDPP 增强语法在最新版兼容基线上通过显式 feature
登记和成熟度管理演进，不改变普通 `.gd` 的官方语义。

## 开发构建

下面这些依赖只面向 GDPP 自身的开发者，不是插件最终用户依赖：CMake 3.22+、
Ninja、Python、C++17 编译器和 Git 子模块。
GitHub Actions 的质量、三平台、Godot 版本、Android、Web 与发布职责见
[流水线职责说明](.github/workflows/README.md)。

```sh
git submodule update --init --recursive
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev
```

构建可直接打开的 Godot 示例项目：

```sh
cmake --preset plugin
cmake --build --preset plugin --parallel
```

然后直接打开 `example/project.godot`，通过 **项目 > 工具 > Build GDPP
Native Library** 编译示例脚本。插件会直接调用 `c++`（Windows 为 `cl.exe`），不经过
CMake。编译器路径可在项目设置 `gdpp/build/cpp_compiler` 中覆盖。

正式发行包由 `release` 预设产生：

```sh
cmake --preset release
cmake --build --preset release --parallel
```

全量发行构建默认串行调度各 Godot 版本的 editor/debug/release godot-cpp 变体，单个变体内部
仍会使用主机并行能力，避免多层 `--parallel` 造成内存与句柄风暴。资源充足且需要自行承担峰值
风险的构建机可显式传入 `-DGDPP_SERIALIZE_SDK_BINDING_BUILDS=OFF`。

macOS 正式包使用 universal SDK：

```sh
cmake --preset release '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'
cmake --build --preset release --parallel
```

插件及项目最终动态库与用户侧编译 SDK 会直接写入 `example/addons/gdpp/binary` 和
`example/addons/gdpp/sdk`，构建后无需复制即可测试；发布取件目录就是
`example/addons/gdpp`。GDPP 自身的 CMake 对象和中间文件集中在仓库根目录 `build/`；示例
Godot 项目的转译源码、对象缓存、清单和测试导出位于 `example/addons/gdpp/build`，Godot 缓存
位于 `example/.godot`。
这些目录均由 `.gitignore` 精确排除，不会把本机二进制或缓存提交到源码仓库。

## 当前能力

当前实现包括词法与语法分析、`std::variant` 强类型 AST、语义检查、Godot 4.4～4.7 多版本 API
元数据、类型化 HIR、显式 CFG/MIR、两级验证与优化、GDExtension C++ 生成、区分实现/公开 ABI
并按真实引用边失效的 manifest v3、对象级原生构建缓存、自包含 ABI SDK、
最新版注解能力注册表、`range`/`len`/`load`/`preload` intrinsic 注册表、精确原生参数 meta 转换、
命名/匿名枚举、带绑定和守卫的 `match`、development/debug 专用 `assert`、Godot 4 内联及方法绑定
属性访问器、`super.method(...)`、分支和循环内 Signal `await` 续体、常用 `@export` Inspector
属性、项目脚本继承图、路径稳定的未命名脚本原生身份、跨脚本强类型成员/常量/枚举、类型化 `_init`、AOT 脚本
`preload/load.new(args)` 与父类优先注册、
构建 ID 原生类隔离、
development/debug/release profile 隔离、场景与资源导出替换、安全源码剥离、macOS universal 构建、
Android arm64-v8a NDK 交叉构建与单库 APK 封装、单物理描述符导出事务、Release 符号裁剪、
Web wasm32 Emscripten side module、`threads`/`nothreads` ABI 与缓存隔离、Web dlink 导出门禁、
Unicode Godot 字符串与 NodePath 生成、`%UniqueNode` 导出语义、Windows 普通 Godot 进程自动
初始化 MSVC 工具链、Godot 4.7 向前兼容实机加载测试，以及 macOS/Linux/Windows CI/CD
骨架。第三方二进制 GDExtension 由 Godot 原样加载，其 addon 脚本与其他 GDScript 一样彻底
AOT；静态类型与原生继承的责任边界见
[第三方原生类型的编译信息](docs/GDEXTENSION_BRIDGE.md)。Node 派生的脚本 Autoload 会在导出时转换成引用构建 ID
原生类的场景；场景 Autoload 会解析根节点脚本，GodotSteam 等第三方 GDExtension 单例通过
`Engine` 的运行时单例表安全解析。直接 `range()` 和十种 PackedArray 的同步循环使用类型化、
零临时 Array/零 Variant 迭代器代码；Release 原生链接启用节级死代码删除，原生对象缓存还会
校验编译器路径与构建策略签名，避免错误复用旧对象。生成的预加载缓存与脚本静态存储会在
Godot scene 级终止前主动清空，避免原生静态析构晚于 Rendering/Physics Server；导出摘要对
实际重建和场景缓存复用均提供可机器读取的状态。内容寻址 development 库只在新库成功链接后
按同平台、架构、profile 和精确 Build ID 规则回收；编辑器、导出、直接编译和生成的 CMake
工程共用同一清理策略，不会扫描或删除未知二进制。

第三方 ClassDB 契约会校验成员参数、返回值、只读属性、静态调用和命名冲突，并按类计算内容
身份；显式离线清单只使实际引用它的脚本失效。示例项目还提供 GDScript/AOT 行为 oracle、
13 类固定微基准、交替启动、峰值 RSS、帧间隔/帧内工作负载和完整发行目录体积矩阵。当前已用
官方 Godot 4.5.2 在 macOS、Ubuntu 24.04 和 Windows 11 实际完成 GDS→AOT→GDS Release
导出、运行 oracle、PCK/动态库审计和性能预算；macOS 主机另完成 Android arm64-v8a Release
APK 交叉构建与包内容审计。Android 尚未连接设备执行真机运行，不能把包级通过表述为移动端
完整认证。Web 已完成 Godot 4.5 API 下两种线程模式的真实 Emscripten 编译、链接、Wasm
结构/入口/共享内存差分和路径泄漏审计；官方 Godot 4.5.2 Web 模板与 Chromium 运行门禁已
进入独立 CI，首次流水线留档前不宣称多浏览器认证。Linux CI 使用官方 Godot 导出物持续执行
固定矩阵。

准确边界见[语言兼容性](docs/COMPATIBILITY.md)、[编译器架构](docs/ARCHITECTURE.md)、
[类型化 IR](docs/TYPED_IR.md)、[商业交付模型](docs/COMMERCIAL_DELIVERY.md)、
[项目构建流程](docs/PROJECT_BUILD.md)、[第三方 GDExtension 桥接](docs/GDEXTENSION_BRIDGE.md)、
[Web 平台支持](docs/WEB.md)、
[跨平台实测报告](docs/PLATFORM_TEST_REPORT.md)、[体积与性能基线](docs/PERFORMANCE.md)和
[路线图](docs/ROADMAP.md)。原生二进制能提高脚本被
直接解包的成本，但不能保证客户端逻辑绝对不可逆向。
