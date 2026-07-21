# 第三方 GDExtension 类型兼容

第三方 GDExtension 本身已经是 DLL、SO 或 dylib，始终由 Godot 负责加载和导出，GDPP 不会
重新编译它。第三方 addon 内的 `.gd` 则与项目其他 GDScript 完全相同，统一进入 AOT、场景替换
和源码剥离流程。

本页只说明一件事：GDPP 如何编译引用第三方原生类的 GDScript。

## 当前结论

| 场景 | 状态 | 处理方式 |
|---|---:|---|
| 安装、加载、导出第三方 GDExtension | ✅ | Godot 按 `.gdextension` 描述符处理 |
| `var data: WaveformData` | ✅ | compiler 插件从运行中的 `ClassDB` 自动采集类型契约 |
| 实例属性、方法、信号、整数常量 | ✅ | 编译期校验，运行时经 Godot Object/Variant ABI 调用 |
| 第三方静态方法 | ✅ | 通过 `ClassDB.class_call_static()` 调用 |
| `WaveformData.new()` | ✅ | 通过 `ClassDB.instantiate()` 实例化并校验可实例化性 |
| `Engine` 中的第三方单例 | ✅ | 从 Godot 运行时单例表查询 |
| CLI 离线编译第三方静态类型 | ✅ | 使用可审计的 Runtime `gdpp_bridge.json` 快照 |
| `extends VendorNode` | ✅ | 附着式 AOT：保留供应商原生对象，把生成行为作为 `ScriptInstance` 附着 |
| 继承附着脚本、外部 `super`、原生回调 | ✅ | 脚本继承继续编译；外部 `super` 使用精确 MethodBind 哈希与 GDExtension ABI |
| 附着 Node/Resource 的场景、资源与 Autoload 导出 | ✅ | 转换为 `AttachedCompiledScript`，保留供应商原生类型和序列化数据 |
| 第三方命名枚举命名空间 | ✅ | 支持限定类型、限定/扁平常量、方法签名和严格成员诊断 |
| 第三方位标志枚举 | ✅ | 保留 bitfield 身份、位运算和 Inspector Flags 提示 |

因此，编辑器内正常加载供应商插件时，普通类型引用和 `extends` 都不要求客户手写契约，也不要求
修改客户脚本、场景、资源或供应商源码。显式 Runtime 清单只用于无 Godot 进程的 CLI、不可加载
目标平台库的交叉构建、可复现审计，或补充供应商没有完整注册到 ClassDB 的元数据。GDPP 没有
假装解决 Godot 尚不支持的跨 GDExtension 原生 C++ 继承，而是复现 GDScript 本来使用的脚本附着
模型。

## Godot 怎样识别第三方类

Godot 对 GDExtension 的正常处理流程是：

1. 读取 `.gdextension`，选择当前平台、架构和构建特征对应的动态库；
2. 调用动态库入口函数；
3. 动态库通过 GDExtension ABI 把类、父类名、方法、属性、信号、枚举和常量注册进 `ClassDB`；
4. GDScript、场景、Inspector 和 `ClassDB.instantiate()` 都通过同一份运行时注册信息认识这些类；
5. 导出时，Godot 根据描述符把目标平台的第三方动态库加入游戏。

所以 Godot 完全认识 `WaveformData` 和 `VendorNode`。GDPP 以前只有官方 `extension_api.json`，
缺的是项目运行时新增的类型表，而不是第三方插件无法被 Godot 识别。

## ClassDB 零配置采集

编辑器内触发编译时，GDPP compiler GDExtension 会在第三方库完成注册后自动：

- [x] 从 `ClassDB` 筛选 `API_EXTENSION` 和 `API_EDITOR_EXTENSION` 类；
- [x] 沿第三方扩展继承链采集到最近的官方 Godot 父类；
- [x] 采集实例属性、只读性、实例/静态方法、默认参数、可变参数、信号、整数常量、命名枚举和
  位标志枚举；
- [x] 保留具体 Object 类型，无法可靠表达的值退化为 `Variant`；
- [x] 从方法参数/返回值的 ClassDB `class_name` 保留 `VendorType.EnumName`，并用属性 getter
  签名回填 ClassDB 属性表中缺失的枚举类型；
- [x] 支持 `VendorType.EnumName.VALUE` 和 Godot 兼容的 `VendorType.VALUE` 两种访问形式；
- [x] 枚举值使用精确 `int64_t`，离线 JSON 不经过 `double`，大于 2^53 的值不会丢失精度；
- [x] `@export` 普通枚举生成 `PROPERTY_HINT_ENUM`，位标志生成 `PROPERTY_HINT_FLAGS`，保留供应商
  声明顺序和显式数值；
- [x] 为每个第三方类生成独立、确定性的契约哈希；
- [x] 只使实际引用且契约发生变化的脚本缓存失效；
- [x] 允许项目内显式 Runtime 清单覆盖自动结果；
- [x] 排除 GDPP compiler 自身和上一次开发构建残留的 `GDPPNative_*` 类；
- [ ] 把编辑器专用扩展类与发行 Runtime 契约拆成两个能力域；
- [ ] 将加载描述符和目标平台二进制哈希纳入导出前一致性审计。

自动结果只在内存中传给项目编译器，不会在客户项目中散落临时 JSON。每个类的构建身份仍会
进入 `addons/gdpp/build/project/bridge.lock` 和脚本内容哈希；该目录本来就是受控中间产物目录。

## Runtime 清单

离线模式可以提交机器生成、可审计的 Runtime 清单：

```json
{
  "schema": 1,
  "provider": "AudioStreamExt.gdextension",
  "abi": "vendor-2.4.1+sha256-...",
  "godot_minimum": "4.4",
  "classes": [
    {
      "gdscript_name": "WaveformData",
      "godot_base": "RefCounted",
      "mode": "runtime",
      "members_complete": true,
      "properties": [
        {"name": "sample_count", "type": "int", "read_only": true}
      ],
      "methods": [
        {
          "name": "sample",
          "return_type": "float",
          "parameters": [{"name": "index", "type": "int"}]
        },
        {
          "name": "format_version",
          "return_type": "int",
          "static": true,
          "parameters": []
        }
      ],
      "signals": [{"name": "changed", "parameters": []}],
      "enums": [
        {
          "name": "Format",
          "bitfield": false,
          "values": [
            {"name": "FORMAT_PCM", "value": 1},
            {"name": "FORMAT_FLOAT", "value": 2}
          ]
        },
        {
          "name": "ChannelFlags",
          "bitfield": true,
          "values": [
            {"name": "CHANNEL_LEFT", "value": 1},
            {"name": "CHANNEL_RIGHT", "value": 2}
          ]
        }
      ]
    }
  ]
}
```

`members_complete=true` 会在 AOT 前拒绝拼错的成员；`false` 只校验清单已知成员，其余成员保守
走动态路径。枚举名、枚举值名必须在类静态命名空间内唯一，值必须是 JSON 整数；重复、空枚举、
非整数或超出 `int64_t` 的输入会使整个契约事务失败。Runtime 契约不包含供应商头文件或链接库，
也不改变第三方库的加载方式。

## 枚举生成语义

以下形式均进入强类型枚举 IR：

```gdscript
var format: WaveformData.Format = WaveformData.Format.FORMAT_PCM
var same_format: WaveformData.Format = WaveformData.FORMAT_PCM
var channels: WaveformData.ChannelFlags = (
    WaveformData.ChannelFlags.CHANNEL_LEFT | WaveformData.CHANNEL_RIGHT
)
```

生成 C++ 使用 `int64_t` 作为稳定的 GDScript 枚举存储 ABI，不依赖供应商 C++ enum 的大小、
命名空间或头文件。第三方方法调用仍经 Godot Variant ABI，返回值和参数在边界转换为强类型枚举。
不同命名枚举之间不能直接赋值；枚举仍遵循 GDScript 对整数显式兼容和位运算的规则。

## `extends VendorNode` 的商业实现

GDScript 继承第三方原生类时，Godot 创建 `VendorNode` 原生对象，再把 GDScript 的
`ScriptInstance` 附着到该对象上。GDScript 没有注册另一个动态库拥有的 C++ 子类，因此不需要
供应商 C++ 头文件。

如果生成类直接在 C++ 中继承 `VendorNode`，就会变成“GDPP 项目动态库中的扩展类继承供应商
动态库中的扩展类”。供应商头文件和链接库只能解决 C++ 编译期声明，不能补上 Godot 对两个
扩展实例层、生命周期、虚方法和重载关系的运行时管理，因此 GDPP 不使用这条不安全路径。

商业后端把两层职责分开：

1. 供应商 GDExtension 仍按原描述符加载并注册 `VendorNode`，Godot 用供应商工厂创建真实对象；
2. GDPP 项目库注册一个 `ScriptLanguageExtension`、`ScriptExtension` 和 `ScriptInstance` 运行时；
3. 每个生成脚本编译成 `AttachedScriptBehavior`，由 `ScriptInstance` 持有并绑定到供应商对象；
4. 脚本字段、方法、属性、信号、通知和 RPC 元数据由生成行为提供，原生属性、方法、信号与生命周期
   仍由同一个供应商对象提供；
5. 供应商从 C++ 通过 `Object::call()` 触发的脚本虚方法会进入附着行为；脚本的外部 `super`
   使用 ClassDB 采集的精确 MethodBind 兼容哈希，经 `object_method_bind_call` 调用原生实现；
6. 附着根类之上的项目 GDScript 继承继续使用同一行为链，不会重新引入跨库 C++ 父类。

这意味着客户现有的 `extends VendorNode`、场景、`.tres`、Autoload 和供应商插件都无需改动。
项目库不会包含供应商头文件或链接供应商库，供应商升级仍保持独立的签名、许可证和发布边界。
生成项目 GDExtension 标记为不可热重载，以避免已附着实例跨二进制世代悬挂；重新编译后由 Godot
重启加载是当前商业安全边界。

失败关闭条件同样明确：供应商描述符和目标平台二进制必须存在，首次构造附着对象时目标类必须已
注册进 ClassDB，外部 `super` 所需方法必须公开反射并具有精确兼容哈希。Godot 会从无序集合
重建扩展注册表，因此项目库的初始化不依赖供应商与项目扩展的先后；生成行为先独立注册，供应商
类型在全部启动扩展完成注册后的脚本实例化阶段才解析。CLI 没有可加载供应商进程时，可使用机器
生成的 `gdpp_bridge.json` 提供相同契约；私有 C++ 方法、未注册 ABI 或无法精确描述的行为不会被
猜测调用，而会在生成或导出前阻断。

把供应商和 GDPP 生成类共同编入同一个 GDExtension 动态库理论上可以绕开“跨库”，但会改变
第三方插件的构建、签名、升级和许可证边界，不适合作为客户零配置方案。

## addon 脚本与交付边界

- 第三方 addon 内的 `.gd` 不再有目录级排除规则，必须与项目脚本一样完成 AOT；
- 第三方 `.gdextension` 和目标平台动态库保留，由 Godot 正常导出；
- compiler、SDK、生成 C++、对象文件和 ClassDB 采集信息不得进入最终游戏；
- 严格导出只有在所有纳入范围的 GDScript 都完成编译、替换和验证后才剥离源码；
- Runtime 契约缺失、供应商类未注册或外部 `super` 缺少精确 MethodBind 哈希时失败关闭，不生成
  猜测 ABI 的 C++；
- 插件许可证是否允许编译其 GDScript 和分发生成二进制，仍需要客户按商业合同确认。

该链路由两个完全独立的 GDExtension 动态库验证：供应商库注册 `VendorBase` Node 与
`VendorResource`，GDPP 项目库只包含生成行为。Godot 4.4～4.7 门禁覆盖脚本多级继承、字段和
`_init`、原生/脚本信号、供应商到脚本的虚调用、脚本到供应商的精确 `super`、通知、RPC 继承/
覆盖、Node/Resource 序列化往返，以及真实无源码导出后的进程运行。macOS 门禁还审计两套动态库
均为 Universal 2、PCK 违规为零且供应商描述符在导出事务后逐字节恢复。

## 官方机制依据

- [Godot ClassDB 文档](https://docs.godotengine.org/zh-cn/stable/classes/class_classdb.html)：运行时类、
  方法、属性、信号、常量和 API 类型查询；
- [Godot GDExtension 文档](https://docs.godotengine.org/zh-cn/stable/tutorials/scripting/gdextension/what_is_gdextension.html)：
  动态库、接口和描述符的职责；
- [Godot 4.5 GDExtension 注册实现](https://github.com/godotengine/godot/blob/4.5-stable/core/extension/gdextension.cpp)：
  跨 GDExtension 父类分支当前标记为未实现；
- [godot-cpp 类注册实现](https://github.com/godotengine/godot-cpp/blob/master/include/godot_cpp/core/class_db.hpp)：
  godot-cpp 静态 C++ 类型和父类注册路径。
