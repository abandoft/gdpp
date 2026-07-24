# GDPP 跨平台实测与缺陷审计报告

本文记录截至 2026-07-21 对当前 GDPP 实现执行的 macOS、Linux、Windows、Android 和 Web
验证。
报告区分“真实运行”“真实构建/导出”和“仅包内容审计”，不把 CI 编译、headless 导出或
APK 生成替代对应平台的用户交互和真机运行认证。测试产生的日志、JSON、APK、PCK 与动态库
统一保存在根目录 `build/platform-matrix/`，不进入源码仓库和客户插件包。

## 验收结论

| 范围 | 环境 | 结果 | 证据级别 |
|---|---|---|---|
| macOS | 官方 Godot 4.5.2、Apple Silicon、AppleClang、Universal 2 | ✅ Release 插件/SDK/项目库、GDS→AOT→GDS 导出、运行 oracle、PCK 与双切片审计通过 | 真实构建、导出、运行 |
| Linux | Ubuntu 24.04 x86_64、官方 Godot 4.5.2、GCC 13 | ✅ 16 / 16 CTest，GDS→AOT→GDS 导出与运行、PCK/ELF/性能门禁通过 | 真实构建、导出、运行 |
| Windows | Windows 11 x86_64、官方 Godot 4.5.2、MSVC 19.50 | ✅ 16 / 16 CTest，GDS→AOT→GDS 导出与运行、PCK/PE/性能门禁通过 | 真实构建、导出、运行 |
| Android | macOS 主机、官方 Godot 4.5.2、NDK 29、arm64-v8a | ✅ Release APK 交叉构建、脚本剥离、ABI、ELF 入口和包内容审计通过 | 真实构建与导出；未连接 Android 设备 |
| Web | macOS 主机、Emscripten 5.0.7、Godot 4.5 API、wasm32 | ✅ `nothreads`/`threads` SDK 与 Release side module 构建、dylink/入口、shared memory 差分和路径泄漏修复通过 | 真实编译与链接；官方模板/浏览器 CI 待首次留档 |
| 独立第三方 GDExtension | 官方 Godot 4.6.2/4.7.1、AppleClang、Universal 2 | ✅ 双扩展编译/加载/运行；4.6.2 无源码 `.app` 导出、PCK、双库切片与描述符恢复通过 | 真实构建、导出、运行 |
| 编译器核心 | AppleClang C++17 | ✅ 419 / 419 单元测试，本机开发 CTest 16 / 16 | 单元与集成测试 |

当前可以确认三桌面平台的固定示例在同一 Godot 版本下行为一致，严格二进制导出不会携带
GDScript、compiler 或 SDK，且已发现的问题均以通用规则和回归测试修复。当前仍不能据此
宣称任意项目、全部 GDScript 语义、所有 Godot 4.4～4.7 组合或全部移动端已经商业认证。

## 测试方法

- [x] 三桌面平台均使用官方 Godot 4.5.2 和本平台正式 C++ 工具链，不使用交叉编译结果代替。
- [x] 每个平台先构建 compiler、对应 SDK 与项目库，再执行完整 CTest。
- [x] 导出顺序固定为 GDScript Release → AOT Release → GDScript Release，专门检测导出缓存、
  描述符事务和场景替换对后续普通 GDScript 导出的污染。
- [x] GDScript/AOT 各执行 AB/BA 交替启动、13 类微基准和固定帧工作量；比较结构化行为
  oracle，并由性能预算自动阻断严重回归。
- [x] 对 PCK 或 APK 枚举全部条目，拒绝 `.gd`、`.gdc`、compiler、fallback、SDK、godot-cpp、
  生成 C++、对象和其他构建中间物。
- [x] 对最终项目动态库检查平台架构、Release 剥离状态和唯一 GDExtension 入口。
- [x] 商业语料使用 Castle Defense、Dungeon Rush 和 NoahEngine，生成和 CMake 中间物位于每份
  根目录 `build/` 下的隔离输出，Windows 语料使用一次性副本，不复用原项目构建目录。
- [x] Windows 跨主机语料在 CP936 与 UTF-8 两种边界下验证非 ASCII 路径；归档解压前切换
  UTF-8，并以中文资源探针失败关闭，编译器内部不再依赖 ANSI `path::string()`。
- [x] 第三方继承 fixture 将供应商和 GDPP 项目构建为两个互不链接的 GDExtension；分别验证
  provider-first 与 project-first 启动、供应商对象身份、附着脚本行为、外部 `super`、回调、
  信号、RPC、Node/Resource 序列化、真实导出进程和零源码泄漏。

## 独立第三方 GDExtension 附着式 AOT

Godot 4.6.2 Universal 2 的最终导出以现有客户工程形态执行：项目脚本仍直接
`extends VendorBase`，供应商描述符、动态库和源码均未修改。供应商库创建真实 `VendorBase`
Node/`VendorResource`，GDPP 项目库通过 `AttachedCompiledScript` 附着生成行为。

| 审计项 | 结果 |
|---|---|
| Godot 4.6.2 导出日志 | ✅ `ERROR` / `WARNING` 为 0 |
| 导出 `.app` 黑盒运行 | ✅ 输出 `GDPP_ATTACHED_EXPORT_RUNTIME_OK` |
| PCK 文件/场景/资源 | ✅ 14 / 2 / 1，违规 0 |
| GDPP 项目 dylib | ✅ arm64 + x86_64 |
| 独立供应商 dylib | ✅ arm64 + x86_64 |
| 供应商/项目扩展加载顺序 | ✅ provider-first 与 project-first 均通过；本次导出为 provider-first |
| 客户脚本、场景、资源改动 | ✅ 0 |
| 供应商源码或构建改动 | ✅ 0 |
| 导出事务残留备份 | ✅ 0，描述符与注册表已恢复 |

Godot 4.7.1 还独立通过同一双扩展的编译、加载和运行门禁。该结果证明受支持的公开反射语义可在
不形成跨动态库 C++ 继承的情况下交付；它不把供应商私有、未注册或没有精确 MethodBind 契约的
C++ ABI 宣称为可调用能力。

## 三桌面平台运行与发行结果

每个平台的行为 oracle 和阈值报告均为通过。帧率受固定约 144 FPS 上限约束，因此“平均 FPS
相近”只能证明没有明显整机回归，不能把微基准倍数直接换算成游戏帧率。

| 指标 | macOS Universal 2（arm64 运行） | Linux x86_64 | Windows x86_64 |
|---|---:|---:|---:|
| GDS / AOT 启动中位数 | 160.814 / 166.336 ms | 164.360 / 166.905 ms | 157.444 / 157.467 ms |
| AOT 启动变化 | +3.434% | +1.548% | +0.015% |
| GDS / AOT 有效 FPS | 145.939 / 145.654 | 143.595 / 144.067 | 145.367 / 144.000 |
| GDS / AOT 帧间隔 CV | 0.03743 / 0.03758 | 0.01575 / 0.00887 | 0.01581 / 0.00000 |
| 固定帧工作量 GDS / AOT | 63.788 / 5.429 ns | 365.279 / 15.749 ns | 374.646 / 15.542 ns |
| 固定帧工作量加速 | 11.75× | 23.19× | 24.11× |
| GDS / AOT PCK | 28,880 / 9,516 B | 28,880 / 9,420 B | 28,880 / 9,420 B |
| GDS / AOT 完整发行物 | 187,677,972 / 188,549,454 B | 70,207,944 / 71,608,300 B | 96,398,544 / 97,017,036 B |
| AOT 完整发行物变化 | +0.464% | +1.995% | +0.642% |
| AOT 项目库 | 974,880 B Universal 2 | 1,419,816 B | 637,952 B |
| GDS / AOT RSS 均值 | 当前采集器不支持 | 78.31 / 79.56 MB | 当前采集器不支持 |

macOS AOT 启动中位数本轮高 3.43%，Windows 的平均 FPS 低约 0.94%；二者未突破当前阻断预算，
但属于后续长时间采样必须继续观察的弱项。Linux RSS 增加约 1.60%；macOS/Windows 没有可靠
RSS 采样，不能写成“内存无增加”。

### 13 类热路径加速比

| 工作负载 | macOS | Linux | Windows |
|---|---:|---:|---:|
| 强类型数值 | 9.93× | 19.95× | 13.79× |
| 强类型分支 | 22.05× | 74.67× | 85.08× |
| `Array` | 1.37× | 1.61× | 1.70× |
| PackedArray | 1.85× | 3.68× | 6.11× |
| `Dictionary` | 1.16× | 1.14× | 1.06× |
| 内建向量 | 12.08× | 38.19× | 64.03× |
| 对象属性 | 2.55× | 2.80× | 1.32× |
| 字符串 | 1.16× | 1.42× | 1.18× |
| 项目方法调用 | 72.76× | 128.99× | 70.99× |
| `Callable` | 1.62× | 1.80× | 1.55× |
| Variant 运算 | 1.45× | 1.98× | 1.84× |
| 信号发射 | 1.07× | 1.51× | 1.05× |
| 分配型集合构造 | 1.76× | 1.64× | 1.77× |

结果表明收益主要来自可静态化的数值、分支、向量和项目方法；仍经过 Godot Variant/容器/信号
ABI 的路径只有小幅收益。商业宣传必须使用实际游戏基准，不得把 108× 的单一方法调用微基准
表述成“游戏整体快 108 倍”。

## Android arm64-v8a

最新代码从 macOS 主机生成了 26,262,814 B 的 Release APK。包内 126 个条目，项目库为
`lib/arm64-v8a/libgdpp.release.android.arm64.so`，大小 520,896 B。

| 审计项 | 结果 |
|---|---|
| 官方 Godot 4.5.2 Android Release 模板导出 | ✅ |
| NDK arm64-v8a C++ 编译和链接 | ✅ |
| `.gd` / `.gdc` 条目 | ✅ 0 |
| compiler / fallback / SDK / godot-cpp 条目 | ✅ 0 |
| GDPP 项目动态库 | ✅ 1 |
| 导出入口 | ✅ 仅 `gdpp_project_library_init` |
| ELF 普通符号表 | ✅ 已剥离，无 `.symtab` |
| Godot `ERROR` / `SCRIPT ERROR` / `WARNING` | ✅ 0 |
| Android 真机安装、启动、输入、音频、生命周期 | 尚未执行；ADB 设备列表为空 |

Android 当前结论是“arm64 Release 交叉构建与 APK 封装通过”，不是完整移动端运行认证。AAB、
商店签名、ABI split、minSdk 覆盖、x86_64、暂停/恢复、后台回收和真实设备性能仍需单独门禁。

## Web wasm32

Godot 4.5 API 的 `nothreads` 与 `threads` target pack 分别为 87.99 MiB 与 90.57 MiB；这些是
用户构建输入，不进入最终网页。固定示例的 Release 项目 side module 分别为 1,015,600 B 与
1,026,366 B，debug 分别为 3,176,015 B 与 3,217,253 B。Web debug 使用 `-g2`
保留函数名与断言，不携带 Emscripten 系统库的不可重映射 DWARF 路径。

| 审计项 | 结果 |
|---|---|
| 10 个示例脚本生成与 12 个项目/运行时编译单元 | ✅ |
| NativeBuilder 实际执行 Emscripten C++17 编译和链接 | ✅ 双模式 |
| debug/Release profile 实际生成 | ✅ 双模式 |
| `dylink.0` 与 `gdpp_project_library_init` | ✅ 双模式 |
| Binaryen `--all-features` 验证 | ✅ 双模式 |
| `nothreads` 导入普通内存 | ✅ `(memory ... 3)` |
| `threads` 导入共享内存 | ✅ `(memory ... 3 65536 shared)` |
| SDK 静态库及项目 debug/Release 绝对源路径 | ✅ 泄漏 0；双层 prefix map；旧 SDK 失败关闭 |
| 静态库单独变更的增量失效 | ✅ 仅 1 条重链命令，0 个编译单元 |
| 官方 Godot 4.5.2 dlink 模板导出、PCK 和 Chromium oracle | 已进入 `web.yml`；本机无模板，待 CI 首次结果 |

该结果证明 GDPP 生成代码可以形成结构正确的两类 Emscripten side module，不证明网页已在所有
浏览器运行。独立 Web CI 会下载官方模板，检查 COOP/COEP、PCK 内容、Wasm 结构并在 Chromium
等待运行时通过 `JavaScriptBridge` 写入 `data-gdpp-status="ok"` DOM oracle；
Safari、Firefox、移动浏览器、CDN、PWA 和性能矩阵仍需
后续认证。详细交付约束见 [Web 平台支持](WEB.md)。

## 本轮发现并按类别修复的问题

| 问题类别 | 根因 | 通用修复与防复发 |
|---|---|---|
| Windows 项目首次扫描崩溃 | macOS 复制产生 `._*` AppleDouble 文件，Godot 会在插件初始化前扫描 | ✅ 发布工作流失败关闭拒绝 `._*`、`.DS_Store`、`__MACOSX`；跨系统测试复制主动净化；`.gitignore` 保持拒绝规则 |
| 无 Android SDK 的 Windows 输出错误 | NDK 自动发现直接枚举不存在的目录 | ✅ 先以绝对路径检查目录存在；桌面-only 环境返回空值；描述符测试锁定 |
| Windows 信号微基准回归 43.81% | 本地脚本信号每次发射都构造临时 `godot::Signal` | ✅ 本地/`self` 信号直接调用 `emit_signal`，调用点缓存 `StringName`，参数仍按源码顺序单次求值；外部接收者保留通用语义路径 |
| Windows 大项目 MSVC `C1060` | 先有无限并行压力；进一步实测发现 37 个源码挂起点形成递归嵌套 lambda 类型，单个 `cl.exe` 峰值约 12 GB，关闭优化仍无效 | ✅ MSVC+Ninja 设置 compile=4/link=1 job pool；长且无需跨挂起局部状态的链按 MIR 基本块生成扁平状态机和弱引用续体，问题单元由 368,708 B/1,416 行降到 156,157 B/692 行并在 MSVC/AppleClang 链接通过 |
| SDK 全量构建过度并发 | 根构建并行启动多个完整 godot-cpp 变体，每个子构建又占满 CPU；SSH 中断后曾留下 9 个 Ninja 树并锁住依赖数据库 | ✅ 默认按 editor/debug/release 与 Godot 版本串行 SDK 变体，单个变体内部继续全核并行；可用 `GDPP_SERIALIZE_SDK_BINDING_BUILDS=OFF` 显式覆盖 |
| Godot 错误日志漏检 | Windows 个人设置中的残缺 Android SDK 输出无 `ERROR:` 前缀，旧规则误判为干净 | ✅ 三平台、Android、运行矩阵和 CI 同时拒绝 `Unable to open`；Windows 使用隔离 APPDATA 和硬链接模板，不读取或修改个人编辑器设置 |
| Windows 非 ASCII 资源重复 UID | `std::filesystem::path::generic_string()` 按 CP936 输出扫描到的中文路径，而 `.import`/场景仍为 UTF-8，同一资源被视为两个所有者 | ✅ 建立统一 UTF-8 路径转换，覆盖脚本/资源/Autoload/场景、CMake、原生编译参数、桥接锁和编辑器服务；加入中文目录与资源 UID 回归，Dungeon Rush 197 单元在 Windows 冷热生成通过 |
| 多构建树测试相互污染 | arm64 与 Universal 测试共享同名临时 `.gdextension` | ✅ 描述符名加入构建目录哈希和目标架构，不再覆盖其他构建树 |
| GDS→AOT→GDS 导出次序污染 | Godot 4.5 导出场景缓存可复用上次 AOT 原生化场景 | ✅ 普通 GDS 导出只清理 `.godot/exported` 场景缓存；插件事务和项目产物保持隔离；固定顺序成为三平台门禁 |
| 独立 PCK 找不到项目扩展 | 包内使用未被 Godot 物理扫描过的虚拟项目描述符路径，强制注册表过滤会删除该条目 | ✅ 编辑态扫描、导出扫描和包内运行共用 `addons/gdpp/gdpp.gdextension` 物理路径，只事务式切换内容；两份描述符各自唯一，运行时覆盖两种加载顺序 |
| Godot 4.6 误报 Universal provider 缺失架构 | 同一 dylib 同时列为 arm64/x86_64 时，导出器在去重后没有更新第二个架构计数 | ✅ 先以 `lipo` 验证双切片，事务式生成仅供扫描的 `universal` 描述符；PCK 写入供应商原始字节，失败/重启均恢复 |
| Release 包公开符号过多 | 平台链接/剥离检查不一致 | ✅ PE/ELF/Mach-O 分别验证唯一入口；macOS 对 Universal 2 的每个切片单独审计；Linux/Android 要求普通符号表剥离 |
| 包内容审计误报 | 早期规则把合法 `addons/gdpp/runtime` 目录名当 compiler | ✅ 改为文件基名、受控目录和项目库例外的结构化规则；仍对 SDK、godot-cpp 和非项目 GDPP 动态库失败关闭 |

信号优化修复后，同一 oracle 下 macOS、Linux、Windows 分别为 1.07×、1.51×、1.05×，未通过
放宽阈值掩盖回归。Android 最新项目库也在该代码生成版本上重新构建并审计。扁平异步状态机
另有 10 次连续挂起/恢复的 Godot 原生运行门禁，逐次检查 checkpoint 1～11，避免只验证 C++
能够编译却改变恢复顺序。

## 商业项目压力测试

本轮 Windows 冷/热压力测试使用与客户项目相同的项目编译入口：Castle Defense 180 单元冷/
热生成 8.09/0.30 秒，Dungeon Rush 197 单元为 5.87/0.18 秒；NoahEngine 在引入供应商脚本
所有权隔离前的 122 单元基线为 7.84/0.17 秒。三个项目的热生成均为全量缓存命中。Castle
另外完成 183 个 MSVC/Ninja
目标的冷链接（87.04 秒）与无变化热构建（2.66 秒），用于验证受控并发、扁平异步状态机和
UTF-8 路径修复能同时消除 `C1060`、重复 UID 和无效增量重建。

macOS 也对三项目执行完整 AppleClang Release 原生链接：Castle 冷/热生成 0.49/0.09 秒、链接
37.29 秒；Dungeon Rush 为 1.05/0.13 秒、链接 35.87 秒；NoahEngine 为 0.33/0.05 秒、链接
26.67 秒。该轮三者分别生成 180、197、122 个 C++ 单元，热生成全部命中，动态库全部链接通过；
峰值编译进程 RSS 分别约 379、463、394 MB。

取消 GDExtension addon 脚本特例后，NoahEngine 的 AudioVorbisExtender 2 个辅助脚本重新进入
同一 AOT 流程；最新 AppleClang 实测恢复为 122 个单元并完整链接通过。

压力测试的时间与最终状态由 `build/platform-matrix/windows/corpus/corpus-results.json` 记录。
该语料是本地不可发布客户 fixture；公开 CI 继续使用固定官方 demo，不能上传客户源码或生成物。

## 仍需完成的商业门禁

- [ ] Android 真机安装、启动、触摸/手柄、音频、暂停恢复、低内存回收和至少 30 分钟稳定性。
- [ ] Android x86_64、AAB、ABI split、minSdk 矩阵和商店签名验证。
- [ ] Windows arm64、Linux arm64、iOS 设备/模拟器；Web 尚缺 CI 首次留档、多浏览器与真实 CDN。
- [ ] Godot 4.4、4.6、4.7 各自的三桌面完整运行差分；4.6.2/4.7.1 已完成 macOS 独立第三方
  GDExtension 主路径，但本轮三桌面完整矩阵仍只认证 4.5.2。
- [ ] debug 导出、编辑器热重载、连续构建/导出、崩溃恢复和长时间磁盘增长压力。
- [ ] 路径矩阵：空格、非 ASCII、超长路径、只读目录和低磁盘空间。
- [ ] 真实游戏的 Boss、敌群、存档、网络、资源流送和长时帧时间 p50/p95/p99。
- [ ] 手写 godot-cpp 对照、分配次数和 macOS/Windows 可靠 RSS 采集。

因此，当前质量等级是“三桌面固定矩阵、Android arm64 包级主路径和 Web wasm32 原生构建主路径
通过”，尚未达到路线图定义的全部平台 1.0 Stable 门禁。
