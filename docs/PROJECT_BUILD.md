# 项目构建流程

## 用户依赖

正式用户只需 Godot 与目标平台 C++ 工具链。插件不会调用 CMake、Python、Ninja、SCons，
也不会在用户机器上生成或编译 godot-cpp。

| 平台 | 必需工具链 | 原生插件与 target pack 编译基线 |
|---|---|---|
| Windows x86_64 | MSVC Build Tools（编译器、链接器、Windows SDK） | Windows 10 |
| macOS arm64 | Xcode Command Line Tools | macOS 11.0 |
| Linux x86_64 | Ubuntu 22.04 基线兼容的 GCC/Clang 开发环境 | Ubuntu 22.04 / glibc 2.35 |
| Android arm64-v8a | Android NDK r28+ | Android 9 / API 28 |
| iOS arm64 | macOS 上的完整 Xcode与对应 GDPP target pack | iOS 16.0 |
| Web wasm32 | Emscripten | 无固定浏览器版本下限 |

这里的系统版本是原生插件、项目库依赖和 target pack 的编译 ABI 契约，不是导出测试预设。
`example/export_presets.cfg` 与 CI 的导出测试不写入 Android `min_sdk`、iOS 最低版本或 macOS
deployment target，而是继承所安装的官方 Godot 导出模板设置；回归测试会拒绝重新加入这些覆盖项。

Android arm64-v8a、iOS 和 Web wasm32 已接入自动导出；Windows arm64、Linux arm64 与
Android x86_64 仍属于平台矩阵里程碑，导出预检和 NativeBuilder 会在生成项目前失败关闭，
不会声明或尝试构建尚未交付的 ABI。
Web 需要匹配 Godot 小版本和线程模式的 target pack，详见 [Web 支持](WEB.md)；iOS 需要包含
真机与 Universal Simulator 依赖的 target pack，详见 [iOS 支持](IOS.md)。

## 示例与发行项目

插件源码位于 `example/addons/gdpp`。开发与正式构建把插件动态库和配套 SDK 直接写入
该目录，因此构建完成后可立即打开 `example/project.godot` 测试，发布时也直接打包该插件
目录。`binary/`、`sdk/`、`addons/gdpp/build/` 与 `example/.godot/` 都有精确忽略规则；CMake 对象和
其他开发中间产物仍统一位于根目录 `build/<preset>`。

单个正式插件包的 SDK 结构如下；`<version>` 只会是 4.4、4.5、4.6、4.7 中的一个：

```text
addons/gdpp/sdk/
├── .gdignore
└── <version>/               # 当前宿主原生绑定、运行时与 sdk.manifest
    ├── android/arm64/       # 三个桌面宿主都包含，最低 Android 9 / API 28
    └── ios/arm64/           # 仅 mac-arm64 包包含，最低 iOS 16
```

正式发布按三个宿主与四个 SDK 版本组成 12 个 ZIP，命名为
`gdpp-<Godot版本>-<宿主>.zip`，宿主固定为 `mac-arm64`、`linux-x64`、
`windows-x64`。构建时通过 `-DGDPP_PACKAGE_GODOT_VERSIONS=4.5` 只生成一个宿主 SDK；compiler
插件仍固定使用 Godot 4.4 ABI 和同一套最新版 GDScript 前端，内置的 4.4～4.7 目标 API 能力表
不会因此裁剪。Android target pack 随三个宿主包交付，iOS target pack 只随 macOS 包交付；
Web 保持独立可选 target pack，不计入这 12 个 ZIP。

每个 ZIP 内的 `PACKAGE_MANIFEST.txt` 记录 GDPP 版本、compiler API、目标 Godot API、宿主、
最低系统和可导出目标。打包器会核对三套 SDK 的 runtime ABI/源码摘要，并失败关闭地拒绝错误
版本、错误平台、缺少移动 target pack 或最低版本不一致的组合。schema 8 还把 C++17、异常关闭、
Windows MSVC 编译器族、19.x 工具集版本、静态 CRT 与 Android `c++_shared` 写成必填 ABI 字段；
SDK profile 构建和独立 provider fixture 共用 compiler、toolchain、sysroot、deployment target、
RC/MT 工具参数，避免嵌套 CMake 配置与预编译 godot-cpp 静态库漂移。

SDK profile 不沿用构建 GDPP 编译器插件时的优化级别。`debug` 静态库始终以 Debug 构建，
`release` 静态库始终以 Release 构建；因此开发者使用 Debug compiler 插件执行 Release 导出时，
项目动态库及其 godot-cpp ABI 依赖仍是完整优化版本。

构建后的插件布局为：

```text
example/addons/gdpp/
├── binary/                  # compiler、fallback 与项目最终原生动态库
├── build/                   # 转译源码、清单、对象缓存和测试导出，不进入发行包
├── sdk/                     # 用户项目直接原生编译所需 SDK
├── gdpp.gdextension         # 编辑态唯一物理 GDExtension 描述符
├── export_plugin.gd
├── plugin.cfg
├── plugin.gd
└── THIRD_PARTY_NOTICES.md
```

### 产物边界

GDPP 只允许在下列位置产生二进制或中间文件，不以“已经写进 `.gitignore`”替代目录治理：

| 目录 | 内容 | 是否交付 | 是否可直接删除 |
|---|---|---|---|
| 仓库根 `build/<preset>/` | GDPP 自身 CMake 对象、库、测试项目和本地 QA 证据 | 否 | 是 |
| `addons/gdpp/build/` | 当前 Godot 项目的生成 C++、清单和对象缓存 | 否 | 是，之后会完整重建 |
| `addons/gdpp/binary/` | compiler、fallback，以及当前项目生成的动态库 | 按角色筛选 | 项目库可删；compiler/fallback 不可删 |
| `addons/gdpp/sdk/<version>/` | 供用户本地链接的头文件、运行时源码和 godot-cpp 静态库 | 是 | 否 |
| 项目 `.godot/` | Godot 导入缓存和 GDPP 导出事务备份 | 否 | 编辑器关闭后可重建 |
| 用户选择的导出目录 | 游戏可执行文件、PCK、项目 release/debug 库 | 是 | 由用户管理 |

`CleanAddonBuild.cmake` 是插件取件清理入口：它带路径保护，只删除 `addons/gdpp/build/` 和
`binary/gdpp_project.*`。CI 与 Release 工作流在归档前强制执行并验证这条规则。用户项目构建
另由编译器核心提供精确的 development 产物回收：只在当前库成功链接后，删除同目录中同平台、
同架构、同 profile、同扩展名且符合 16 位十六进制 Build ID 命名的旧库。未知文件、符号链接、
其他架构以及 debug/release 产物不会被触碰；Windows 正被进程占用的 DLL 删除失败时保留并报告，
不能把清理失败伪装成构建失败或扩大删除范围。

GDPP 公开构建 profile 与产物角色固定如下：

| Profile | 用途 | 文件名示例 |
|---|---|---|
| `development` | 开发期由 Godot 加载；使用内容构建 ID，成功链接后精确回收同目标旧库 | `libgdpp_project.development.macos.universal.<BuildId>.dylib` |
| `debug` | 调试导出；稳定文件名，保留断言并关闭优化 | `libgdpp_project.debug.macos.universal.dylib` |
| `release` | 正式导出；稳定文件名，关闭断言并优化 | `libgdpp_project.release.macos.universal.dylib` |

插件本体使用 `libgdpp_compiler.<platform>.<arch>`，不会再把 `editor` 写入产品文件名。Godot
要求 `.gdextension` 使用 `editor/debug/release` feature key，godot-cpp 也有自己的 ABI target
名称；两者只存在于内部适配层，不属于 GDPP 的公开 profile。

## 编辑器构建

启用插件后选择 **项目 > 工具 > Build GDPP Native Library**。流程为：

```text
发现项目 .gd
    -> 建立 class_name/脚本路径继承图与成员签名表并按父类优先排序
    -> 增量转译并事务式更新生成 C++
    -> 校验 SDK 平台和架构
    -> 为变化源文件生成编译器参数数组
    -> 直接执行 c++ / cl.exe
    -> 链接 res://addons/gdpp/binary 下的项目 development 动态库
    -> 重新扫描 Godot 资源
```

默认编译器在 Unix 类平台为 `c++`，Windows 为 `cl.exe`，可通过
`gdpp/build/cpp_compiler` 指定绝对路径。调用不经过 shell，带空格的路径作为独立参数传递。

对象文件位于
`res://addons/gdpp/build/project/native-direct/<version>/<platform>/<arch>/<profile>/objects`。
不同 API、平台、架构及 development/debug/release profile 绝不误用彼此的对象。当生成源文件、相关头文件
和目标 ABI 静态库都未变化时，编译和链接命令会完全跳过。每个原生缓存目录还保存
`build-configuration.txt`，内容包含原生构建修订、API、平台、架构、profile 和编译器绝对路径；
任一项变化都会强制重编译，不能只依赖容易受时钟偏差影响的文件 mtime。Release 在 MSVC 使用
`/Gy /Gw /OPT:REF /OPT:ICF`，在 GCC/Clang 使用 section 拆分和 dead-strip/gc-sections。Windows
生成的 Ninja 工程使用可配置的 `GDPP_MSVC_COMPILE_JOBS` 作业池（默认 4，链接 1），即使调用者
传入 `--parallel` 也不会在大型脚本项目上无限并发 `cl.exe`、耗尽编译器堆空间。

项目生成清单使用 `GDPP_MANIFEST 3`。清单头记录插件版本、代码生成契约、项目 Build ID，并为
每个脚本分别保存实现哈希、公开 ABI、实际引用脚本和第三方 provider ABI。公共头文件、前端、
语义、HIR/MIR、后端、runtime、Godot 4.4～4.7 API 表或生成工具发生不兼容变化时，旧清单会被
安全拒绝；普通方法体变化只重建自身，公开签名/Inspector 契约变化只传播到实际依赖方。无变化
时清单、生成 C++、对象和最终动态库均保持不写入，供热缓存门禁验证。

`res://addons/gdpp/binary/` 保存全部最终动态库；生成 C++、清单、描述符和对象缓存位于
`res://addons/gdpp/build/project/`。插件发行流程会删除整个 `build/` 工作区，并从 `binary/`
移除 `gdpp_project.*`，只交付 compiler 与 fallback，不会把示例游戏库打进插件包。

每一组脚本内容与 API 基线会产生 16 位内容构建 ID，动态库文件名包含该 ID。脚本变化时会
链接到新文件，而不是覆盖编辑器进程可能仍在使用的 DLL；这避免了 Windows 文件锁，并为
后续安全热重载提供稳定边界。项目原生类名也包含同一构建 ID，确保旧动态库残留在编辑器
进程时不会向新场景替换暴露过期的信号、方法或导出属性。

内容寻址不等于永久保留所有历史库。直接编译服务、编辑器菜单、导出器和生成的项目 CMake
都调用同一套精确回收规则；CMake 路径通过 `POST_BUILD` 执行，因此编译或链接失败不会先删掉
上一个可用库。当前库以及 compiler、fallback、debug、release、其他平台/架构和不符合受控命名
语法的文件均不会被清理。该规则避免脚本修改持续扩大客户目录，也让 CLI/CMake 与编辑器行为
保持一致。Godot 调用方使用高层 `execute_project_build` 完成“执行、产物验证、成功后清理”事务，
底层多进程调度只保留为 compiler GDExtension 的私有实现，不向 GDScript 绑定可绕过收尾的原始
命令接口。

项目内 `extends SomeGlobalClass`、`extends "res://base.gd"` 和相对脚本路径会解析为生成类之间
的真实 C++ 继承。缺失父类、与 Godot 类型重名或继承循环会在写入清单前失败；注册代码严格按
父类到子类排序。继承字段、实例方法与静态函数会进入同一成员签名表参与静态检查。

项目 `class_name` 可直接用于字段、函数参数和返回值。编译器会生成对应的原生指针或 `Ref`、
跨脚本字段访问器和静态函数调用，并校验继承赋值、参数数量及参数类型。成员签名表摘要进入
所有脚本缓存键，因此父脚本只改变 ABI、使用方源码不变时，使用方仍会重新语义分析和生成。

协程身份也是公开 ABI 的一部分。项目扫描先建立保守摘要，再以完整语义分析迭代到固定点，因而
`await 42` 这类立即表达式不会把普通 `int` 方法误标为原生协程；真实挂起方法则以 `Variant`
入口返回立即结果或每次调用独立的完成 Signal。已知协程调用缺少 `await` 会在提交生成清单前以
GDS4132 失败。同步基类方法被异步派生方法覆盖时，派生实现使用独立 C++ 符号，ClassDB 仍发布
原 GDScript 名称；基类类型接收者改走动态派发，避免 C++ 仅返回类型不同的非法覆盖。协程身份
改变会重命名目标原生类并只使真实依赖方失效，单纯协程方法体修改仍复用公开 ABI。

跨脚本常量使用 `Other.CONST`，命名枚举使用 `Other.State.MEMBER`，匿名枚举值使用
`Other.MEMBER`，三者在符号表中保持不同种类；限定枚举类型可出现在字段、参数和返回值中。
枚举数值同时用于 C++ 常量、`match` 和 Inspector hint。项目脚本静态方法会注册到 ClassDB，
因此原生实例化和 Godot 动态调用路径均能使用一致签名。字面量项目脚本路径的
`preload`/`load` 会生成编译期资源句柄，`.new(args)` 校验 `_init` 参数并直接分配目标原生类，
不在导出包中查找 `.gd`。全默认参数 `_init` 可由 ClassDB 正常零参数实例化；含必需参数时生成类
保留默认构造供 Godot 注册，显式工厂调用使用类型化构造。动态加载路径和非脚本资源当前失败。

断言按构建 profile 隔离：`development` 和 `debug` 定义 `DEBUG_ENABLED`，会执行
`assert(condition, message?)` 并在失败时报告原脚本位置；`release` 定义 `NDEBUG`，生成
C++ 中的断言块会被预处理器完全移除，条件与消息表达式也不会求值。条件和消息都允许 Signal
`await`：调试构建先恢复条件，只有失败时才连接并恢复消息，成功时直接进入唯一的后续业务续体；
Release 的实际 C++ 预处理门禁会验证两类表达式、错误报告和 Signal 连接均不存在。

## 单描述符导出事务与失败安全运行时

`binary/libgdpp_fallback.<platform>.<arch>` 是不注册任何项目类的极小 GDExtension。编辑态插件
目录只保留 `gdpp.gdextension` 这一份物理描述符，避免 Godot 在扫描目标平台时同时发现
compiler、fallback 和项目库。导出开始时，插件备份并暂时把该描述符改写为目标项目库或
fallback 的扫描描述符；处理文件列表时跳过编辑态内容，并通过 `add_file()` 向成品中的同一
物理路径 `res://addons/gdpp/gdpp.gdextension` 写入项目运行描述符。导出结束或下次启动时恢复
原文件，因此项目目录不会长期处于导出态，Godot 的扩展注册表也始终引用一个真实存在的稳定
运行入口，而不是会被导出器过滤掉的虚拟第二路径。

启用二进制源码保护时，AOT 编译、原生类加载或导出替换失败会保留
原 `.gd` 但默认阻断导出，不能把 fallback 当作静默交付源码的理由。只有显式打开
`gdpp/allow_source_fallback` 才允许失败后交付普通脚本包。成功导出时，包内描述符只引用当前
debug/release 项目库，fallback 不进入成品；fallback 不包含 compiler、SDK 或用户项目逻辑。
包内运行描述符的发布条目按运行架构唯一匹配：Windows/Linux 使用目标架构；macOS 通用包使用
`arm64` 与 `x86_64` 两个 Godot feature key，并让二者指向同一个 Universal 2 动态库。
Godot 导出阶段会把 `universal` 作为目标架构扫描 `.gdextension`，因此导出器临时生成只含
`macos.*.universal` 的扫描描述符，避免同一双切片库被识别两次；写入 PCK 的运行描述符恢复为
`arm64`/`x86_64`。compiler 扫描描述符、包内项目描述符和 GDExtension 注册表都由同一事务保存、
改写并恢复，异常退出时也通过备份恢复，不把扫描态留在客户工程。

项目中存在第三方 GDExtension 时，事务会确保供应商和 GDPP 项目运行描述符各自唯一登记，并在
Godot 保留顺序时优先登记供应商。由于 Godot 4.4～4.7 可从 `HashSet` 重建注册表，附着运行时
同时把相反顺序作为正式支持路径：生成行为注册不依赖供应商，首次实例化才验证 ClassDB 原生类。
Godot 4.6 在同一 Universal 2 dylib 同时列为 arm64/x86_64 时会错误报告缺失切片；GDPP 先用
`lipo` 验证两套切片，再只为导出扫描临时把该供应商条目归一化为 `universal`。成品仍写入供应商
原描述符的逐字节内容，供应商源码、动态库和项目设置均不修改；成功、失败和下次启动恢复路径
共同清理事务备份。

## 自动导出

Godot 开始桌面导出后，`EditorExportPlugin` 在打包文件前执行：

```text
增量生成并构建 development 项目库
    -> 加载内部 GDPPNative_* 类供导出定制使用
    -> 按导出平台、架构和 debug/release 构建 template 库
    -> 校验稳定目标动态库存在
    -> 保留供应商描述符与动态库，并让 provider 在项目运行时之前加载
    -> 按 SceneState/Resource 序列化存储图替换普通或附着式 GDScript 实例
    -> 递归转换内嵌 Resource、Array/Dictionary 与 PackedScene，保留共享/循环引用
    -> 第三方 Node/Resource 保留供应商原生类型并附着 AttachedCompiledScript
    -> 原子保留存储属性、动态 metadata、节点层级、owner、分组及信号 flags/binds/unbind
    -> 写入只引用项目 template 库的运行时描述符
    -> 从包中排除项目 .gd、编译器、SDK 和全部 AOT 中间文件
```

导出期生成的 Autoload 原生或附着式场景只作为包内虚拟资源写入
`res://addons/gdpp/runtime/autoload/`，不会复用或暴露 `build/` 工作区路径。
生成类在实例字段初始化前将自身 `ObjectID` 登记到启动注册表；运行时查找正常优先走
SceneTree，仅在节点尚未命名或挂入树的构造窗口使用注册表。因此按 Godot 项目设置顺序创建的
后置 Autoload 可以安全读取前置 Autoload，注册表又不会用裸指针或强引用延长对象生命周期。

导出库使用稳定名称，例如
`libgdpp_project.release.macos.universal.dylib`；它与 development 的内容寻址名称分开，便于
Godot 导出器在固定描述符中定位。导出期间临时过滤的 GDExtension 注册表和全局脚本类缓存会
在 `_export_end` 原样恢复。

每个导出预设提供 `gdpp/strip_gdscript_sources`，默认启用。关闭后保留普通 GDScript 工作流。
启用剥离时 `gdpp/allow_source_fallback` 默认关闭，任一步失败都会生成阻断错误并跳过客户文件；
显式打开它才允许普通脚本回退。目前自动原生导出覆盖 macOS、Linux、Windows、Android
arm64-v8a 和 Web wasm32；macOS 正式发行 SDK 支持 universal，Android 由 NDK 交叉编译并只把
目标 `.so` 与 Godot 所需 `libc++_shared.so` 交给 APK 导出器。Web 先用宿主 development 库完成
场景转换，再用 Emscripten 生成 thread-mode 匹配的 debug/release side module；导出预设未启用
Godot Web GDExtension 支持时直接阻断。

Godot 4.5 的场景导出缓存目录没有正确使用 scene customizer configuration hash。GDPP 同时把
转换修订和项目构建 ID 放入导出插件名称，确保代码生成器或场景转换器更新后进入新的缓存空间，
不会复用失败导出留下的原始 `.scn`。ClassDB 收集原生默认值时，生成构造函数只执行可证明纯净
的字面量、集合和 Godot 值类型初始化；单例、资源加载和用户调用仍延迟到非编辑器运行期，兼顾
场景默认值压缩与导出安全。

生成代码不得依赖 C++ 进程退出阶段才析构 Godot 资源。项目扩展在
`MODULE_INITIALIZATION_LEVEL_SCENE` 的 terminator 中按注册逆序清空函数局部 `preload()`
缓存和脚本 `static var` 存储，使 Resource、RID 和容器在 Rendering/Physics Server 仍存活时
释放。导出摘要无论重新转换场景还是复用 Godot 场景缓存都会输出，并用 `cache` 字段标记状态。

## 失败与兼容语义

- 编译器不可执行、SDK 不完整、API/平台/架构不匹配时，在启动 C++ 编译前失败。
- GDScript 批次存在错误或原生类重名时，不更新翻译单元清单。
- C++ 编译器输出完整回显到 Godot 输出面板，非零退出码立即停止构建。
- 插件最低版本为 Godot 4.4；项目描述符按目标写入 4.4、4.5、4.6 或 4.7。
- compiler 插件不会被设计为游戏运行时依赖。
- 编译、原生类加载、场景/资源替换或目标校验任一步失败时不剥离 `.gd`；商业二进制预设默认
  阻断，只有显式 `gdpp/allow_source_fallback=true` 时才按原脚本运行。
- Node 派生的脚本 Autoload 会在导出期生成原生场景并事务式改写项目设置；设置在导出结束后
  恢复。非 Node 脚本 Autoload 会触发安全回退。
- Android arm64-v8a 已通过 macOS 主机交叉构建和 APK 零源码审计；尚未完成真机运行、x86_64、
  AAB/ABI split 与商店签名认证。
- Web 已完成 Godot 4.5 API 下 `threads`/`nothreads` 的真实 Emscripten 编译链接、dylink/入口、
  shared-memory 差分和路径泄漏审计；官方模板导出及 Chromium 门禁已进入 CI，Safari、Firefox、
  移动浏览器和真实 CDN 尚未认证。
- iOS 16+ 已实现 device arm64、Simulator arm64/x86_64、动态 XCFramework 和事务式提交；独立
  CI 负责四个 Godot 目标包及 Godot 4.6.2 无源码 Xcode 工程/无签名 Simulator 构建。真机、
  Apple 签名、TestFlight 和 App Store 仍必须在客户发布环境认证。
