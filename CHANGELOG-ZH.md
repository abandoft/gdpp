## 1.7.1

- 完成强类型可变参数函数、构造器、方法、lambda、反射元数据、调用 thunk、缓存指纹和附着式脚本分派，并贯通 Godot 4.4～4.7；语法始终由 GDPP 自有前端解析，不依赖宿主 GDScript parser。
- 完成跨脚本 preload 命名空间，覆盖根类与嵌套类、枚举、常量、静态字段/函数、强类型资源引用、转换、类型测试、继承和规范化 Inspector 枚举身份。
- 加固跨版本原生对象 lowering，覆盖 RefCounted 值、裸 Object 指针、singleton 包装、属性 getter 真实返回类型、显式类型化 null 分支、动态调用和协程返回 ABI。
- 运行时与导出发现链现可解析脚本及场景 Autoload UID，事务式重写被剥离的 Autoload，拒绝 editor-only 运行资源图，并隔离 editor-only 生成注册。
- 统一以受保护赋值处理 Dictionary、强类型容器、String、StringName、NodePath、Array、PackedArray、Callable 和 Signal，消除原生资源滞留与自赋值损坏；附着式脚本描述符遵循同一规则。
- 集中传递嵌套 CMake 的 compiler、toolchain file、sysroot、target triple、Apple deployment、MSVC CRT、RC/MT、generator platform 和 toolset；独立构建的 provider 扩展可与打包 SDK 保持一致 CRT 并完成链接。
- 发行 SDK 升级至 schema 7 / runtime ABI 8，对 C++17、异常模型、MSVC 静态 CRT、Android `c++_shared`、源码完整性、平台、架构、profile 和运行时契约执行失败关闭校验。
- 修正 Godot 4.4 兼容门禁：仅允许由 GDPP 新语法触发且精确匹配的宿主内置 parser 提示，所有其他导入、导出、运行时与 PCK 诊断仍严格失败。
- 新增 Godot 4.4 真实原生压力测试，覆盖引用型值自赋值及动态、强类型、静态 Dictionary 的重复释放，并在 macOS、Linux、Windows 独立构建 provider SDK 消费者。
- 使用官方 Godot 4.6.1 复验二进制交付链，覆盖 AOT Autoload、独立第三方 GDExtension 启动、导出包运行，以及源码、compiler、SDK 零泄漏。

## 1.7.0

- 新增 GDScript 继承独立第三方 GDExtension 类型的零源码改动 AOT 支持，无需重新构建、重新链接或修改供应商插件。
- 引入基于 `ScriptLanguageExtension`、`ScriptExtension` 与 `ScriptInstance` 的附着式 AOT 后端：原生 Object 仍由供应商创建和持有，生成的 C++17 行为负责脚本字段、方法、属性、信号、通知和 RPC 元数据。
- 保留附着式原生根类之上的脚本继承，包括字段初始化、`_init`、方法/属性分派、虚回调、信号行为，以及 RPC 配置的继承与覆盖。
- 外部 `super` 调用使用供应商反射出的 MethodBind 精确兼容哈希和稳定 GDExtension ABI，不再采用不安全的跨动态库 C++ 继承或猜测签名。
- 扩展 ClassDB 采集与 `gdpp_bridge.json` 校验，纳入供应商身份、精确可调用元数据、与加载顺序无关的供应商解析、缓存失效，并在关键反射缺失时失败关闭。
- 二进制导出期间把场景、独立资源、内嵌资源和 Autoload 转换为附着式编译脚本，同时保留供应商原生类型与序列化状态。
- 导出游戏逐字节保留第三方描述符和目标平台动态库，provider-first 与 project-first 两种注册表顺序均可安全启动，不要求客户项目或供应商源码改动。
- 加固 macOS Universal 2 导出：事务式规范化供应商扫描描述符、逐 Mach-O 切片验证、崩溃恢复，并在导出后恢复所有源描述符。
- 新增完全独立的双 GDExtension fixture，并为附着式交付链建立 Godot 4.4～4.7 真实构建、加载、运行、导出、PCK 内容、二进制架构与零源码泄漏门禁。
- 发行 SDK 升级至 schema 6 / runtime ABI 7，对宿主、桌面、移动与 Web 目标中的全部附着式运行时头文件和源文件执行摘要校验。

## 1.6.0

- 完成 Godot Variant 完整类型域、可空性模型和零值真值语义，并贯通语义分析、类型化 HIR、生成的 C++17 与原生运行时。
- 集中定义数值、字符串、内建值、PackedArray、对象、Ref、RID 和容器的严格赋值、显式转换、分析器兼容、运行时可构造与原生存储规则。
- 支持 `Array[T]` 与 `Dictionary[K,V]` 参数化 `as` 目标、跨脚本限定枚举转换目标、常量严格转换检查和运行时受检转换；对原生存储无法保持的确定性 String/RID 路径在生成前失败关闭。
- 强类型容器存储改为精确校验运行时元素、键、值、对象类和脚本元数据，不再依赖 godot-cpp 的隐式转换构造器，同时保持 GDScript 分析器对未约束容器和 PackedArray 边界的接受行为。
- 为必然失败的常量转换、直接违反不变规则的强类型容器、确定性运行时转换失败及 Object 背书 RID 等原生存储不可表示路径增加稳定诊断。
- 扩展生成代码严格编译与 Godot 4.7.1 GDScript/AOT 真实差分，覆盖全部真值类别、内建转换、PackedArray、强类型容器元数据恢复、String 转换分歧和 RID 存储行为。
- 隔离嵌套警告控制作用域并按固定格式化器统一分析器源码，保持 Clang、GCC 与 MSVC 发布门禁下的警告即错误构建。
- 严格类型改造暴露限定枚举与运行时容器边界回归后，恢复固定节奏游戏和角色扮演游戏项目的完整编译。
- 将编译器的抽象脚本元数据贯通至 ClassDB 导出校验，并为宿主、Android、Web 与 iOS SDK 统一补充所需运行时注册接口，使二进制导出正确保留原生抽象契约。
- 原生运行时 ABI 升级至 5，发布新的受检转换和强类型存储契约。

## 1.5.0

- 新增端到端流敏感类型系统，覆盖 `is`/`is not`、空值与真值检查、短路表达式、`if`/`while`、三元表达式、守卫后支配路径、结构化 `match` 和带守卫绑定。
- 引入稳定符号身份与分支状态格，提供保守汇合、直接赋值失效、Callable 分析隔离和大型逻辑链有界分析。
- 类型化 HIR 同时保留有效类型、真实存储类型和非空证明，使 C++ 后端能够专门化 Variant 存储读取且不改变原生存储 ABI。
- 为静态解析的方法调用和属性读取加入携带源码位置的 null/已释放对象检查，避免生成未经保护的原生解引用。
- 完成默认参数降级与 Callable 元数据，覆盖省略实参分派、override 兼容性、Callable 参数数量校验及精确 Godot 虚函数 ABI 适配。
- 实现 GDScript 语言工具的常量与运行时契约，包括 `assert`、`is_instance_of`、`type_exists`、`convert`、`str_to_var` 和 `var_to_str`。
- 对未解析值、类型和注解常量执行失败关闭，并常量折叠导出属性 hint/usage 元数据，同时保留 Godot 脚本变量标识。
- 在字段、方法、访问器、`super`、节点简写和嵌套 lambda 中统一执行静态上下文归属规则，同时保留合法静态访问和内部类访问。
- 显式顺序化所有急切求值的二元操作数，使生成的 C++17 在原生值、Variant、成员测试、幂和比较表达式中保持 GDScript 从左到右的副作用顺序。
- 扩充严格生成 C++ 编译和真实 Godot 4.7 GDScript/AOT 差分，覆盖类型收窄、Callable ABI、语言工具、注解、静态上下文和求值顺序。

## 1.4.0

- 将前端常量求值、HIR 优化、生成的强类型 C++ 与动态运行时整数运算统一到同一套可移植 64 位契约。
- 明确定义回绕溢出、归一化移位、有符号除余边界、复合赋值和防溢出的原生范围终止，不再触发 C++ 未定义行为。
- 将发行 SDK 升级到 schema 5 / runtime ABI 4，为所有宿主和导出目标打包并校验共享整数语义头的哈希。
- 新增真实 Godot GDScript/AOT 整数差分、严格生成 C++ 编译，以及阻断式 Linux UBSan 核心流水线。
- 加固 Chromium 交付验证，可靠处理异步 profile 落盘竞态，同时保留严格的清理失败报告。

## 1.3.0

- 完成浮点计数及 Vector2/Vector2i、Vector3/Vector3i 数学范围的原生 `for` 循环支持，方向、步长和边界行为与 Godot 保持一致。
- 新增基于 Godot `_iter_init`、`_iter_next`、`_iter_get` 协议的静态对象迭代支持，在编译期验证协议并保留迭代结果类型。
- 强化类型化循环安全性，对数组和字典字面量执行上下文类型化，精确诊断非法元素、键和循环变量声明。
- 扩充 Godot 4.7 官方兼容门禁及真实 Godot GDScript/AOT 差分测试，覆盖类型化、数学范围、容器和自定义对象迭代。

## 1.2.0

- 新增强类型数组与字典的完整原生支持，在语义分析、跨脚本接口和生成的 C++ 中保留元素、键与值类型契约。
- 完善强类型容器安全检查，覆盖赋值、参数、返回值、导出属性、嵌套容器及项目依赖边界。
- 优化生成代码，直接构造原生强类型容器字面量并消除冗余包装拷贝，降低分配与转换开销。
- 扩充编译器、工程与 Godot 差分测试，覆盖强类型容器行为、ABI 稳定性、依赖失效和运行性能。

## 1.1.0

- 大幅提升最新版 GDScript 兼容性，完善字符串、数字、Unicode 标识符、局部常量、尾随逗号和 lambda 等语法支持。
- 完整支持数组、字典、嵌套及剩余项 `match` 模式，并贯通类型检查、优化与原生代码生成。
- 完善 `await` 与协程编译，支持返回值、立即完成、并发调用及继承场景下的动态分派。
- 强化多脚本工程编译，改进类型与调用关系分析，实现更精确的依赖失效和增量重建。
- 提升编译器安全性与诊断质量，加入 Unicode 关键字防伪、非法输入处理及前端资源上限保护。
- 改善跨平台导出兼容性，继承 Godot 官方模板设置，并加入固定 Godot 4.7 官方语料回归测试。

## 1.0.0

- 首个版本，提供 GDScript AOT 编译器和 Godot 编辑器插件。
