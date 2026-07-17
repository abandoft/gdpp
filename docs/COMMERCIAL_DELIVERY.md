# 商业交付模型

## 发行物

每个平台和架构单独发布 `addons/gdpp`，macOS 发布 universal 包。发行物包含 compiler 插件、
编辑器/导出脚本、预生成头文件、原生绑定静态库、空回退运行时、运行时
源码、许可证和 SDK 清单，不包含用户脚本或游戏项目二进制。

`binary/` 同时是本机最终动态库输出目录。制作插件发行包前，受保护的清理脚本会删除
`addons/gdpp/build/` 和 `binary/gdpp_project.*`，保留 compiler 与 fallback。fallback 是开发期
单描述符导出事务和显式普通 GDScript 预设使用的空运行时；成功 AOT 游戏导出不会携带它。启用源码
剥离的商业预设默认是 fail-closed：AOT 任一步失败都会阻断交付，而不是自动改成交付源码。

编译器插件自身不进入游戏。用户项目的 development、debug 与 release 动态库由目标项目本地
生成，最终游戏只携带对应 debug/release 项目原生库和运行时描述符，不携带 GDPP 编译服务、SDK
或生成 C++。

编辑态插件目录只有 `gdpp.gdextension` 一个物理 GDExtension 描述符。导出事务临时用目标
项目库或 fallback 内容替换它参与 Godot 扫描，成品则写入虚拟的
`gdpp_project.gdextension` 运行描述符，最后恢复编辑态文件。该模型消除“两个描述符同时扫描、
同一项目库重复装包”的歧义，异常退出也由 `.godot` 下的事务备份在下次启动恢复。

Release 项目库和 compiler/fallback 正式库执行死代码删除、局部符号裁剪和入口白名单。ELF
使用 version script 与 `--exclude-libs`，Mach-O 使用 exported-symbol list，Windows 依赖
明确 `dllexport`；发布审计必须确认项目库只公开 `gdpp_project_library_init`。

公共插件包默认携带 4.4～4.7 全部 SDK；面向已锁定引擎版本的单项目交付只保留对应版本。
例如 Godot 4.5 Windows x86_64 客户包只携带 Windows compiler/fallback 和 `sdk/4.5`，避免把
客户不会使用的其他版本 godot-cpp 静态库一并交付。

## Windows 4.5 客户交付基线

2026-07-14 已使用 Windows 11 x86_64、官方 Godot 4.5.2 和 MSVC 对 Dungeon Rush 商业项目
完成实机交付门禁：

- [x] 客户插件只保留 Windows x86_64 compiler、fallback、Godot 4.5 SDK、插件脚本、描述符和
  许可证，不保留 `addons/gdpp/build/` 或 `binary/gdpp_project.*` 临时项目库。
- [x] 普通 Godot 进程能够自动发现 Visual Studio/Build Tools 并初始化 `vcvars`，客户不需要
  从 Developer Command Prompt 启动，也不需要另装 CMake、Ninja、Python 或 godot-cpp。
- [x] 客户侧保留的 4.5 SDK 约 303 MiB，其中主要体积来自 development/release 两份
  godot-cpp 静态库；它们只参与本地链接，不进入导出的游戏。
- [x] Dungeon Rush 共 197 个外部及内嵌脚本单元生成 development/release 项目库；最新 Release
  游戏只增加一个 4,206,592 B（约 4.01 MiB）的 `gdpp_project.release.windows.x86_64.dll`。
- [x] 最终 AOT PCK 共 1953 个项目文件，`.gd`/`.gdc`、compiler、SDK、生成 C++、对象文件和 fallback
  泄漏数为 0。
- [x] Release 成品先越过黑屏和工作室启动画面，再对非 UI 背景区域执行 0.8 秒连续帧采样，
  主菜单滚动动效差值为 11.80；两轮菜单/关卡交互中的设置、移动、翻滚、攻击、背包、暂停冻结、
  暂停内设置、恢复、退出到菜单和再次开始均正常，窗口响应及运行错误门禁通过。
- [x] Windows 音频会话存在且未静音；菜单音乐峰值约 0.142，关卡内背包 UI 音效峰值约
  0.418，不是只检查音频文件是否进入 PCK。
- [x] 动态属性/下标赋值对值类型副本执行完整反向回写；除主菜单滚动背景外，同一修复覆盖激光
  与碰撞体缩放、角色/武器翻转、商店 UI 缩放、背包符文和道具状态等商业项目路径，并有独立
  Godot 运行回归验证对象属性、脚本成员/局部 Variant、深层 Transform3D 和 Array/Dictionary
  容器路径，而非只检查生成文本。
- [x] 建立同机 ori/aot Release 对照：最新 AOT PCK 为 64,131,952 B，比 ori 增加 182,212 B；
  已确认增长来自原生场景属性序列化，不是脚本或构建物泄漏。
- [x] 复制场景存储属性时识别 Godot 动态 `metadata/<key>`，任何其他缺失原生属性都会在修改
  场景树之前原子失败，不再静默丢数据。
- [x] 针对 Godot 4.5 场景定制缓存未把 configuration hash 正确混入缓存目录的问题，将转换修订
  和项目构建 ID 同时写入插件名称；修复编译器/导出器后不会复用失败导出留下的旧 `.scn`。
- [x] 最新完整审计加载 1953 个包内文件和 331 个场景/资源，违规数为 0，完整导出目录恰好
  一个 GDPP 项目动态库。
- [x] 运行 1800 帧后由 scene 级 terminator 逆序清空成员 `preload()`、`const preload` 缓存和
  脚本静态存储；强制退出日志中的 Resource、RID、脚本及 Rendering/Physics Server 生命周期
  错误均为 0。
- [x] development 库使用内容哈希避免覆盖已加载映像；旧扩展成功卸载且新扩展加载完成后自动
  回收历史 development 动态库，避免长期开发把 `binary/` 变成无上限缓存。
- [x] macOS Universal 2 将导出扫描契约和运行契约分离：Godot 以 `universal` 目标扫描时临时
  使用唯一的通用库条目，最终包内再由 `arm64` 与 `x86_64` 两个运行 feature 指向同一个双切片
  compiler/fallback 或项目库。正式导出包仍只携带一个目标项目动态库，连续导出不会出现
  GDExtension 架构选择警告，临时描述符由事务恢复。

完整体积、导出耗时、启动、FPS、CPU 和内存数据见[体积与性能基线](PERFORMANCE.md)。当前结果
不支持“AOT 必然减小 PCK”或“任意游戏显著提高 FPS”的宣传。

Godot 4.5 AOT 完整导出曾在退出时延迟销毁整棵继承场景并产生 `changed` 信号错误。GDPP 现已
在扩展仍注册时后序释放仅供导出的临时场景；清空场景转换缓存后的完整重导出确认编辑器和
控制台日志中的 `ERROR`、`SCRIPT ERROR`、`WARNING` 均为 0。

最终交付采用三重门禁：导出前用 Godot `--import` 验证插件和项目脚本可解析；导出时要求退出码
为 0、出现 `GDPP_AOT_SUMMARY` 且日志无 `ERROR`、`SCRIPT ERROR` 或未解释 `WARNING`；导出后在
独立空项目中加载 PCK 全部场景/资源，并审计完整目录只含一个项目原生库。任何一项失败都不能
发布。主动卸载仍被转换缓存引用的开发扩展可能破坏 Godot 4.5 的对象
生命周期，因此 GDPP 不在导出结束时强制卸载；转换缓存以构建 ID 和转换修订校验，失配时自动
重建，不能用日志白名单掩盖缓存或扩展生命周期错误。

Godot 4.5.2 macOS 在全新 `.godot` 缓存下直接执行 headless `--import` 时，实测可能在退出阶段
把延迟的 GDExtension 文档生成留到 `Main::cleanup`，并崩在 `EditorHelp`；空项目不触发，同一
扩展项目完成一次正常编辑器迭代后也不再触发。CI 因此先执行有界的
`--headless --editor --quit-after 120` 冷缓存预热，再执行独立 `--import` 门禁。预热命令必须正常
退出并完成首次扫描；其职责是建立缓存，不是发布判定。正式 `--import` 必须退出码为 0 且没有
资源、脚本或扩展错误，不能把第一次崩溃后重试成功当作正常流程。

同一问题也适用于项目导入资源。Dungeon Rush 在删除 `.godot` 后直接导出时，Godot 4.5.2 会先
报告自定义字体的 imported 文件不存在，稍后完成导入、返回 0 并生成可完整加载的 PCK。GDPP
不会把这个“最终似乎可用”的包视为合格成品：客户自动化必须依次执行冷缓存预热、独立
`--import` 零错误预检和正式导出零错误门禁，不能从全新缓存直接跳到导出。

Godot 某些版本在导出插件主动阻断后仍可能返回进程退出码 0，因此三重门禁中的三个信号必须
同时判定，不能把“退出码为 0”单独视为成功。`GDPP_AOT_SUMMARY` 在真正重建和 Godot 复用场景
转换缓存时都会输出，并用 `cache` 字段区分两者。

## 支持承诺

- 最低 Godot：官方标准精度 4.4。
- 支持 API：同一 compiler 插件支持 4.4、4.5、4.6 和 4.7，项目库按目标版本选择独立 SDK。
- 自定义 API/double precision：需要独立 SDK，不属于通用发行包。
- 桌面用户依赖：目标平台完整 C++ 工具链，而不只是孤立的编译器可执行文件。
- 交叉平台用户依赖：相应 sysroot、SDK 和交叉编译器。

## ABI 与供应链

godot-cpp 是 C++ 静态库，发行包必须与用户侧编译器 ABI 兼容。每个版本 SDK 清单记录 API、平台、架构、
可用 profile、编译器族和版本；当前代码强制校验 API、平台、架构与
development/debug/release profile，编译器
ABI 精确匹配和友好迁移诊断仍需继续完善。发布 CI 必须使用固定 runner/toolchain、固定子模块
提交，并为归档生成哈希。

SDK 内部仍会看到 godot-cpp 上游定义的 `editor` 和 `template_release` 静态库名；它们只是 ABI
适配细节。GDPP 公开 API、日志、缓存目录和项目动态库文件名只使用
`development`、`debug`、`release`。

后续商业发布门禁包括：

- Windows Authenticode、macOS 签名与公证钩子；
- SPDX/CycloneDX SBOM 和依赖许可证集合；
- 可验证来源证明、归档 SHA-256 和构建日志；
- 符号分离、崩溃符号服务器输入和版本化诊断包；
- API/ABI 兼容检查与至少一个旧 Godot 小版本回归。

## 安全声明

仅在所有目标原生库构建成功、内部原生类可实例化、场景/资源脚本实例原子替换成功且目标文件
通过校验后，导出插件才删除已编译的 `.gd`。GDExtension addon 内的 GDScript 不享有目录级
例外，必须和其他脚本一样成功 AOT；缺少第三方静态类型信息或使用未支持语法时严格导出失败，
不能静默保留供应商脚本。`gdpp/strip_gdscript_sources=true` 且默认
`gdpp/allow_source_fallback=false` 时，失败会注入阻断错误、跳过客户文件并使正式导出不可用；
不会以“可运行”为理由暗中交付源码。确需兼容回退的客户必须显式打开该选项，或使用关闭源码
剥离的独立预设。Node 派生脚本 Autoload 会转为原生场景，不能安全替换的引用按同一策略处理。

导出插件自身如果语法损坏，Godot 可能只给出警告后继续普通脚本导出，因此源码保护不能仅依赖
插件内逻辑。发布 CI 已加入插件解析预检、严格失败故障注入、导出日志检查与 PCK/目录黑盒审计；
客户交付脚本也必须执行同样门禁。

原生编译能阻止直接解包读取脚本文本，并显著提高逆向成本，但不能承诺客户端逻辑不可逆。
商业文案、错误提示和文档必须保持这一边界。
