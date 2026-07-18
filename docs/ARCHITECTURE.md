# 架构

GDPP 将编译器、用户侧构建驱动和 Godot 集成分离。编译器核心不依赖 Godot，Godot
插件只负责编辑器生命周期、路径转换和进程执行。

```text
GDScript 源码
    -> 诊断与缩进词法分析
    -> 强类型 AST 与语义分析
    -> 项目继承图与跨脚本成员签名表
    -> Godot 4.4～4.7 目标 API 能力校验
    -> 类型化 HIR、显式 CFG/MIR、验证与优化
    -> GDExtension C++
    -> NativeBuilder 工具链计划
    -> 系统 C++ 编译器与链接器
    -> 项目原生动态库
```

## 代码组织与依赖边界

`include/gdpp` 与 `src` 使用同一套模块名称。跨 target 使用的接口进入 `include/gdpp/<module>`，
仅供单个适配层使用的头文件保留在对应 `src` 子目录；除生成的 `version.hpp` 外，不允许重新增加
平铺头文件。

```text
include/gdpp/                 src/
├── core/                    ├── core/
├── frontend/                ├── frontend/
├── semantic/                ├── semantic/
├── ir/                      ├── ir/
├── codegen/                 ├── codegen/
├── compiler/                ├── compiler/
├── project/                 ├── project/
├── runtime/                 ├── runtime/
└── support/                 ├── support/
                              ├── integration/godot/
                              └── cli/
```

主编译链的依赖只能单向流动：

```text
core → frontend → semantic → ir → codegen → compiler → project
support（独立基础工具） ───────────────────────────────────→ project

runtime（生成代码 ABI，独立） → integration/godot、用户项目动态库
compiler/project             → integration/godot、cli
```

`support` 是无业务状态的基础工具根，不得反向依赖编译链；`runtime` 只依赖 godot-cpp，不得依赖
编译器实现。`integration/godot` 与 `cli` 是宿主适配层，不允许把 Godot 类型泄漏进 `gdpp_core`。
`tools/check_architecture.py` 在本地测试和质量流水线中检查目录集合、平铺文件和跨层 include，防止
后续功能迭代破坏这些边界。

| 模块 | 职责 | 主要内容 |
|---|---|---|
| `core` | 无业务依赖的编译器基础模型 | 源文件/范围、诊断、目标 Godot 版本 |
| `frontend` | 源码到强类型 AST | token、lexer、parser、语言特性注册、常量表达式求值 |
| `semantic` | 类型与符号解析 | 类型系统、语义分析、项目脚本符号、intrinsic、Godot API 能力表 |
| `ir` | 与语法解耦的中间表示 | 类型化 HIR、显式 CFG/MIR、lowering、验证和优化 |
| `codegen` | 原生代码后端 | 仅消费验证后的 MIR 并生成 GDExtension C++ |
| `compiler` | 单文件编译门面 | 串联阶段、失败事务和分阶段性能指标 |
| `project` | 商业项目级编排 | 依赖发现、精确增量失效、第三方契约、原生构建计划 |
| `runtime` | 生成代码 ABI | `Variant` 运算、动态调用、迭代、await 与 Autoload 支持 |
| `support` | 可复用且无业务状态的工具 | UTF-8 路径边界、SHA-256 |
| `integration/godot` | Godot 私有适配 | compiler GDExtension 服务、注册入口和导出 fallback |
| `cli` | 命令行宿主 | 参数解析、文件 I/O 和终端诊断输出 |

## 关键组件

- `include/gdpp/support/path_utf8.hpp`：UTF-8 文本协议与原生文件系统路径的唯一转换边界；Windows
  内部保留 UTF-16 `path`，禁止用 ANSI `string()` 序列化 Godot 路径、清单或进程参数。
- `include/gdpp/frontend/literal.hpp`：数字字面量的唯一语义规范化入口；整数基数、分隔符和范围
  在前端解析一次，浮点复现 Godot 固定解析算法并写入 HIR 规范值。codegen 只消费规范值，禁止
  反向依赖 frontend 或让不同宿主 C++ 编译器重新解释客户源码。
- `include/gdpp/frontend/unicode.hpp`：Unicode 标识符和安全骨架的唯一入口；
  `tools/generate_unicode_identifiers.py` 从固定 Godot 4.7 stable `char_range.cpp` 及 Unicode 17.0
  `UnicodeData.txt`、`DerivedCoreProperties.txt`、`confusables.txt` 生成 XID、NFD、默认忽略字符和
  UTS #39 映射。运行时使用静态有序表二分并实现 Hangul/NFD，不依赖宿主 locale、ICU 或平台
  字符库，却与 Godot 4.7 ICU 78 的关键字易混淆拒绝规则一致。前端仍保留原始 UTF-8 身份，后端
  对非 ASCII、C++ 保留字及内部前缀做完整字节十六进制编码，得到确定性、单射的 C++17 名称。
- `include/gdpp/frontend/limits.hpp`：前端唯一资源预算契约；源码、单行、Token、字面量、缩进、
  分组、parser 递归和详细诊断上限由 `CompileOptions` 注入 lexer/parser/diagnostic，避免各阶段
  采用互相矛盾的常量。超限属于事务失败，不允许提交翻译单元、项目清单或局部生成物。
- `src/runtime`：生成代码使用的动态 `Variant` 兼容层，集中实现动态运算、受检方法调用、
  命名属性、键/下标读写、三阶段迭代协议，以及只保存 `ObjectID` 的原生 Autoload 启动注册表。
- `ProjectCompiler`：确定性脚本发现、项目继承图与拓扑排序、区分实现哈希和公开 ABI 哈希的
  v3 SHA-256 清单、事务式多脚本生成、重名/缺失父类/循环检查与陈旧翻译单元清理。
- `ExtensionBridge`：解析项目内第三方 `gdpp_bridge.json`，校验 runtime/native 模式、成员契约、
  Godot 基类、目标库和 canonical 路径边界，并把实际引用的供应商 ABI 加入精确失效图。
- `ScriptSymbolTable`：保存全局脚本类型、路径稳定的原生类映射、字段/函数签名、静态成员、常量、
  命名枚举、匿名枚举值、Autoload、资源路径与继承关系，为语义分析、C++ 类型选择和跨翻译单元
  头文件依赖提供同一事实来源；不同目录的同名脚本不会复用原生身份。
- `NativeBuilder`：生成 AppleClang/Clang/GCC/MSVC 编译与链接命令，验证 SDK 平台和架构，
  API 与 development/debug/release profile，并按 profile 隔离、复用仍然有效的对象文件；
  构建策略修订和编译器绝对路径进入缓存配置签名。iOS 后端以四级阶段图构建真机 arm64、
  模拟器 arm64/x86_64，合并 Universal Simulator dylib 后生成动态 XCFramework，并在完整成功后
  事务式替换客户产物。
- `GDPPCompiler::execute_project_build`：Godot 侧原生构建事务入口，统一执行计划、检查链接退出码、
  验证预期动态库并在成功后执行范围清理；菜单、导出和集成测试不再各自组合易遗漏的收尾步骤。
- `tools/generate_godot_api.py`：开发构建期间把 4.4～4.7 官方 API 转换为带版本命名空间的
  编译器内嵌常量表，并保留参数的 `real_t`、定宽整数等原生 meta；最终用户不会执行该脚本。
- `cmake/GodotPlugin.cmake`：集中定义 Godot 插件、fallback、SDK 打包和集成测试 target；测试
  描述符由当前 target 路径生成，禁止集成测试误加载发行目录中的陈旧 compiler；`src` 子目录
  不维护分散的 `CMakeLists.txt`。
- `example/addons/gdpp/export_plugin.gd`：导出预检、目标库构建、基于序列化存储图的
  PackedScene/Resource 与脚本 Autoload 原生替换、注册表事务恢复、Godot 4.5 缓存隔离、
  严格失败阻断和源码剥离。
- `example/addons/gdpp`：插件源码唯一位置，也是当前平台二进制和 SDK 的直接输出与发行取件
  位置。
- `example/addons/gdpp/binary`：compiler、fallback 和项目最终原生库的统一输出目录。
- `example/addons/gdpp/build`：示例项目转译源码、清单、对象缓存和测试导出；不进入插件发行包。
- `example/.godot`：Godot 本地缓存，不纳入版本控制。
- `build/<preset>`：CMake 对象、编译器中间文件和测试临时产物。
- `third/godot-cpp`：固定提交的上游子模块，只在插件开发与发行构建中使用。

## 语言演进策略

GDPP 不维护 Godot 4.4、4.5、4.6、4.7 等多套官方语法方言。编译器前端始终跟进项目声明支持的
最新稳定版 GDScript 语法，旧语法只要仍被最新版接受就自然兼容；官方删除或改变语义的结构通过
迁移诊断处理，而不是永久保留多套 parser 分支。

语言模式只有两个产品层级：

- `GDScript compatible`：最新版官方 GDScript 语法与语义，是兼容基线。
- `GDPP enhanced`：在兼容基线上增加明确登记的 GDPP 扩展；扩展具有实验/稳定状态、lowering、
  runtime shim、诊断和测试，但不伪装成某个官方语法版本。

语法与目标 Godot API/ABI 是独立概念。最新版纯语法只要能等价降低，就可以生成面向 Godot 4.4
的原生代码；引用 4.5～4.7 才存在的引擎 API 时，仍由目标 API 能力表决定接受、shim 或拒绝。
因此编译选项只需要语言模式、GDPP 扩展集合和目标 Godot 版本，不引入官方语法版本选择器。

跟进新的 Godot 稳定版时，发布门禁必须同时更新最新版语法语料、标准库/全局语言能力、API
metadata、目标 SDK 和差分测试。未经认证的新引擎版本不得静默映射到最近的旧能力表。

## 语言核心当前结构与收敛方向

强类型 AST、最新版注解/intrinsic 注册表以及显式 CFG/MIR 已进入编译主链；长期扩展仍不能继续
把节点实现集中追加到大型语义和代码生成文件。后续按以下职责继续收敛，仍由根目录单一
`CMakeLists.txt` 组织，不要求为 `src` 子目录增加 CMake 文件：

- 前端：表达式、语句和 match pattern 已使用具有明确字段的 `std::variant` 强类型节点和统一
  visitor；`await` 只存在一种强类型表达式节点，不再以专用语句和变量标志表达同一语义；旧
  `kind()/operand()` 只作为只读迁移适配器。每个新增节点必须具有源码范围、打印快照和错误恢复测试。
- 语义：在现有注解 `LanguageFeatureRegistry` 与 `IntrinsicRegistry` 上继续拆分声明、表达式、
  控制流、注解、协程和类型检查；`@rpc` 已将位置参数规范化为独立 `RpcConfiguration`，HIR 与
  后端只消费这份强类型契约，不重新解释字符串；新增注解或 intrinsic 必须先登记，GDPP 增强
  特性的成熟度、lowering、shim 与 runtime ABI 仍需在同一注册模型上扩充，禁止重新增加互不一致
  的字符串分派。
- IR：类型化 HIR 后已经生成基本块、前驱、终止指令、挂起边和副作用分类明确的 MIR，并在后端
  前验证。HIR 在建图前以 A-normal form 拆分 `await`，用稳定临时值保存挂起前按左到右顺序
  已求值的操作数；短路、三元和循环条件使用分支 ANF，异步循环把会跨迭代写入的外层局部值、
  函数参数和属性 setter 参数提升到共享单元，并用显式恢复路径处理 `break`/`continue`。match
  分支显式拥有模式绑定后的 `guard_prefix`，MIR 区分模式测试、守卫挂起、真假回退和正文控制边；
  C++ 后端用单一弱回指索引调度器线性生成所有分支和公共外层续体。debug-only assert 的条件与
  惰性消息分别拥有专用 HIR 前缀和 MIR 真假边，公共后续业务只生成一次，Release 预处理后不残留
  表达式求值或 Signal 连接。实例方法协程在 HIR 函数和调用表达式上携带显式协程身份；原生入口
  统一返回“立即值或每次调用独立的完成 Signal”，MIR 仍以源返回类型验证，后端仅在 ABI 边界使用
  Variant。完成路径、断言失败和 Signal 连接失败都必须终结同一状态，不能遗留永久等待者。同名
  shadow 局部仍等待统一协程帧。下一步将所有权、稳定值 ID 和优化载荷从 HIR 下沉，逐步形成 SSA
  或等价数据流表示。
- 后端：拆分表达式、语句、类注册、协程和 Godot 绑定 emitter；后端只消费验证后的 IR，新增
  语言特性不得绕过 lowering 直接读取 AST。无跨挂起局部状态的长 `await` 链已按 MIR 基本块
  生成有界程序计数器状态机，避免嵌套 lambda 导致 MSVC 超大内存和编译失败；一般协程帧仍需
  活跃变量分析后扩展，不能将当前安全子集当作完整协程实现。
- 项目编译：v3 manifest 已按每个脚本保存实现哈希、公开 ABI 哈希和真实引用边；方法体变化不
  改变原生类身份，公开 ABI 变化只传播到直接/传递依赖方，原生对象再由真实 include/depfile
  失效。协程身份通过语义固定点从保守语法摘要收敛，`await` 立即值不会误改原生 ABI；同步基类
  与异步派生覆盖使用独立 C++ 符号和 ClassDB 动态派发，避免仅返回类型不同造成非法 C++ 覆盖。
  后续增加强连通分量调度和跨进程并行锁。
- 性能与质量：固定 GDScript/AOT workload 已度量 13 类运行热路径、启动、峰值 RSS、帧工作量
  和发行体积，并用 oracle/预算阻断回归；后续继续拆分 lexer、parser、semantic、IR、codegen、
  C++ 编译和链接，增加手写 godot-cpp、fuzz、sanitizer 与完整语言/平台差分后才构成 Stable 门禁。

## 两类构建环境

GDPP 自身使用 CMake、Python 和 godot-cpp 生成器，以保证开发构建可复现。发行 SDK
只包含预生成 godot-cpp 头文件、运行时源码、ABI 匹配静态库和 `sdk.manifest`；用户侧不会
重新生成或构建 godot-cpp。

`sdk/<version>/sdk.manifest` 固定 API、平台、架构、编译器族和版本。`NativeBuilder` 在生成
命令前验证 API、平台与架构，防止错误版本或平台静态库进入链接。发行包按操作系统和架构
分别构建，不能混用。macOS universal SDK 的静态库同时包含 arm64 与 x86_64，直接构建命令也
显式传递两组 `-arch`，不会把单架构库伪装成 universal。

公开构建 profile 是强类型的 `development`、`debug`、`release`。只有 ABI 适配层把它们映射
到 godot-cpp 上游 target；文件名、SDK manifest、Godot 编译服务参数和对象缓存目录不得暴露
`template_*`。目标 profile 同时决定优化契约：`debug` 绑定固定使用 Debug，`release` 绑定固定
使用 Release，不能继承 GDPP compiler 插件本身的父构建类型。这样 Debug 编译器也不会把
未优化的 godot-cpp 库链接进客户 Release 游戏。

## 不变量

- 所有源码范围信息贯穿前端，诊断指向原始文件。
- 任一编译阶段失败都不输出半成品翻译单元。
- 整个变化批次通过后才更新项目清单。
- 未支持语法明确报错，不能静默复制或改变语义。
- C++ 后端只接受验证通过的类型化 IR。
- 动态成员 IR 必须显式区分方法和属性；生成代码必须按 GDScript 顺序且只求值一次接收者、
  参数、键和复合赋值旧值。
- Variant `for` 必须将迭代器推进放在循环更新边，确保 `continue` 与普通落出路径行为一致；
  Callable 参数必须先按源码顺序物化，再执行 `.call(...)`。
- 用户侧构建命令使用参数数组，不经过 shell 字符串拼接。
- Godot 资源、项目清单、桥接锁、CMake 文本和进程参数统一使用 UTF-8；文件系统路径只在
  `path_from_utf8`/`path_to_utf8` 边界转换，不能依赖 Windows 活动代码页。
- Windows 生成工程必须用有界 MSVC 作业池治理并发，不能把调用者的无界 `--parallel` 直接
  放大为大型项目上的编译器内存耗尽。
- GDPP 自身的全量发行构建一次只调度一个完整 godot-cpp SDK 变体，变体内部继续并行；不能将
  Godot 版本数、profile 数和主机构建并行度相乘后同时争用内存、进程句柄与 Ninja 数据库。
- SDK 的 debug/release godot-cpp 静态库必须分别来自 Debug/Release 原生构建；CTest 直接审计
  每个版本的独立 CMake 缓存，父构建类型不得污染目标 profile。
- 所有项目中间产物位于 `res://addons/gdpp/build/`。
- 项目最终动态库位于 `res://addons/gdpp/binary/`，与中间对象和转译源码分离。
- 项目动态库使用内容构建 ID 命名，变化构建不会覆盖仍被编辑器加载的旧库；成功链接后只清理
  同平台、同架构、同 profile 且符合精确 16 位构建 ID 命名的陈旧 development 库，未知文件、
  符号链接、debug/release 和其他目标一律保留。
- 项目脚本父类必须先于子类生成和注册；缓存重写会同步更新父子原生类的构建 ID。
- 每个翻译单元只记录语义阶段实际引用的脚本和第三方扩展契约；依赖脚本公开 ABI 或所引用桥接
  契约改变时不能复用旧语义结果，无关脚本变化不能扩大失效范围。
- 编译器版本、前端、语义、IR、代码生成、项目编排、runtime、公共头文件和版本化 Godot API
  表共同形成确定性代码生成契约指纹；v3 manifest 指纹不匹配时必须全量重建，不能依赖人工
  递增修订号。
- 项目脚本 `preload/load` 在编译期绑定目标原生类；成功的二进制导出不需要保留 `.gd` 资源。
- 原生 Autoload 必须在执行实例字段初始化前登记自身 `ObjectID`；查找优先使用 SceneTree，
  启动窗口内才回退到注册表，以保留 Godot 声明顺序下前置 Autoload 的可见性且不延长对象生命。
- 生成的预加载缓存和脚本静态存储必须由 scene 级 terminator 主动清空，不能依赖晚于 Godot
  Rendering/Physics Server 的 C++ 静态析构顺序。
- `_init` 签名同时驱动语义参数检查和 C++ 构造生成；必需参数脚本仍保留 ClassDB 所需默认构造，
  仅显式 `.new(args)` 执行带参数初始化。
- `assert` 只在 development/debug profile 生成可执行检查；release 在预处理阶段连同条件和
  消息求值一起移除，失败诊断保留原始 GDScript 路径与行号。
- 用户 `class_name` 与 ClassDB 原生名分离；项目内部类保留 `Outer.Nested` 形式的限定源身份，并
  扁平化为 `GDPPNative_<ClassName>_<BuildId>__Outer__Nested` 原生名，避免同名嵌套类碰撞或遮蔽
  GDScript 全局类，并隔离热构建前后的信号、方法和属性 ABI；生成和注册顺序由内部类继承图
  拓扑排序，且全部先于宿主脚本类。
- 项目脚本静态方法既进入跨脚本签名表，也通过 `ClassDB::bind_static_method` 注册；匿名枚举值与
  普通常量使用不同符号种类，跨脚本和继承访问必须生成相同的枚举存储名称。
- Godot 构造器、方法、属性访问器和工具函数在重载解析后保存被选中的精确参数 meta；生成器在
  GDScript 类型转换之后再执行 `real_t`、float/double 和定宽整数 ABI 转换，禁止依赖 C++ 的
  隐式窄化。
- debug/release 库使用稳定名称，运行时描述符和原生库进入导出包，compiler 插件、SDK、生成 C++
  与 `.gd` 不进入成功的二进制导出包。
- 编辑态只允许 compiler 一个物理描述符；导出事务临时改写它用于目标扫描，并向包内虚拟写入
  稳定运行描述符，结束或异常恢复后还原。fallback 只为显式 GDScript/失败扫描提供可加载入口；
  成功 AOT 导出不得携带它。
- compiler、fallback 与项目动态库的公开符号必须由平台白名单收敛到各自唯一 GDExtension
  入口；静态 godot-cpp/C++ 运行时符号不得泄漏或在 ELF 多扩展之间发生符号抢占。
- 同一运行架构只能匹配一个 fallback 动态库。Godot 导出器用 `universal` 表示 macOS 通用导出
  目标，但最终 arm64/x86_64 进程分别暴露对应运行 feature；导出扫描描述符只用
  `macos.*.universal` 选择一次双切片库，包内运行描述符则让 `arm64` 与 `x86_64` 分别指向
  同一个 Universal 2 动态库。
- 场景替换必须先准备整棵替换计划再修改节点树；普通存储属性缺失、metadata 复制失败或连接
  恢复失败均不得提交部分转换结果。
- 导出器只遍历 SceneState 和 Resource 属性列表声明的序列化存储值；内嵌 Resource、
  Array/Dictionary、共享/循环引用和内嵌 PackedScene 递归转换时必须保留图身份，不能调用
  与序列化无关的用户 getter 或遍历运行时对象图。
- 启用源码剥离且未显式允许 source fallback 时，任何 AOT 失败都必须生成不可发布结果；Godot
  退出码、日志和导出后黑盒审计共同决定交付成功，不能只信任单一信号。
- 第三方二进制 GDExtension 默认由 Godot 原样加载，无需再次 AOT；其 addon 内的 GDScript 与
  其他项目脚本使用同一编译、替换和剥离规则。编辑器内的 Runtime 静态类型由 ClassDB 自动
  采集，包括强类型命名枚举、bitfield、方法枚举签名和 Inspector hint；CLI 可读取
  `gdpp_bridge.json` 离线清单。Godot 能让 GDScript 附着到供应商原生类，
  但当前不能安全注册跨动态库 GDExtension C++ 子类，因此 Native 清单和供应商 SDK 不作为
  `extends` 的解决方案；未知或跨扩展原生基类以 `PRJ0018`/`PRJ0022` 阻断严格无源码交付。
  完整边界见[第三方原生类型的编译信息](GDEXTENSION_BRIDGE.md)。

## 目标版本策略

插件以 Godot 4.4 为最低运行 API，并内置 4.4～4.7 API 注册表。编辑器自动选择当前引擎对应
目标，用户也可显式选择较低运行基线。语义分析中的引擎成员校验、SDK、构建 ID、对象目录和
生成描述符使用同一目标版本；4.7 API 生成的库明确要求 Godot 4.7，4.4 目标则保持最低向后
兼容边界。该目标版本不改变前端接受的最新版 GDScript 语法。
