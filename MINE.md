# GDPP

现代化、高性能的GDPP插件

- 使用CPP17编写，跨平台
- 使用CMake组织和构建代码
- 高质量和高完成度的商业级项目
- 通过Godot-CPP实现插件化（设置为third目录里的子模块）、全平台编译支持
- 全新实现的GDScript编译器（转译为GDE CPP代码），超高性能
- 在低版本Godot上也可以使用最新的GDScript语法
- 拓展为像Python一样完善和强大，且兼容原先的语法
- 建立完整的测试和`CI/CD`体系（GitHub CI）

用户只需要导入插件就可以编译gds代码，在分发的时候只有二进制文件，防止脚本被解包

## 工程状态

项目已进入持续开发。架构边界、当前兼容范围和分阶段交付门槛分别记录在：

- `docs/ARCHITECTURE.md`
- `docs/COMMERCIAL_DELIVERY.md`
- `docs/COMPATIBILITY.md`
- `docs/PROJECT_BUILD.md`
- `docs/ROADMAP.md`

任何“已支持”能力都必须有自动化测试；未完成语义不得静默转译。

当前已贯通 `std::variant` 强类型 AST、项目语义、类型化 HIR、显式 CFG/MIR、验证与优化、
Godot 4.4～4.7 目标 API、GDExtension C++ 生成、manifest v3 精确引用失效、第三方
GDExtension Runtime/Native 桥接、统一类注册、预生成 ABI SDK，以及编辑器一键直接调用 C++
工具链。语言主路径包括命名/匿名枚举、带绑定和守卫的 `match`、development/debug 专用
`assert`、Godot 4 属性访问器、常用 `@export` Inspector 属性、项目脚本继承、跨脚本强类型
成员/常量/枚举、类型化 `_init`、AOT 脚本 `preload/load.new(args)`、结构化 Signal `await`、
内部类和 lambda。

Node 派生脚本 Autoload、场景/资源图替换、安全源码剥离、构建 ID 原生类隔离和
development/debug/release profile 已进入交付链。正式用户不依赖 CMake、Python、Ninja、
SCons 或单独 godot-cpp。固定 Godot 4.5.2 GDScript/AOT 矩阵已持续比较行为 oracle、13 类
热路径、启动、峰值 RSS、帧工作量和发行体积；完整语言语义、4.4～4.7 全平台运行认证、
fuzz/sanitizer、长时间压力及移动/Web 仍按路线图推进，不把固定矩阵外推为任意项目保证。

插件源码统一放在 `example/addons/gdpp`。`plugin`/`release` 预设把当前平台插件二进制和
用户 SDK 直接写入该目录，开发测试直接打开 `example/project.godot`，发布也从插件目录取件。
CMake 中间产物仍位于根 `build/`，示例运行缓存和 AOT 产物由 `.gitignore` 单独管理。编译器
核心规则集中在根 `CMakeLists.txt`，Godot 集成规则集中在 `cmake/GodotPlugin.cmake`，`src/`
子目录不维护分散的构建文件。
