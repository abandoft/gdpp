# Godot API 元数据

最低版本为 Godot 4.4。开发构建分别读取稳定的 4.4、4.5、4.6 与 4.7 官方 API 快照，运行
`tools/generate_godot_api.py` 在 `build/<preset>/generated/` 下生成四套确定性 C++ 常量表，
随后一起编译进 compiler 插件。发行用户不需要 Python，也不会看到生成步骤。

数据库索引引擎类、内建类、继承关系、方法、参数、返回值、构造器、属性 getter/setter 和
全局单例。语义分析据此验证调用并把解析结果写入类型化 IR，例如：

```cpp
set_position(get_position() + delta);
godot::Input::get_singleton()->is_action_pressed("ui_accept");
godot::Color::html("ff8800");
```

`CompileOptions` 为每次编译携带 4.4、4.5、4.6 或 4.7 目标，语义分析只查询对应数据库。例如
仅在高版本加入的 API 在较低目标中不可见，并会在生成 C++ 前稳定报错。
目标版本进入项目内容哈希、对象缓存目录、SDK 清单校验和 `.gdextension` 的
`compatibility_minimum`，因此不同版本不会错误复用原生对象或向旧引擎泄漏新 API。

自定义 Godot 可能改变 API 或浮点精度，必须从目标引擎导出匹配 API 并构建独立 SDK，不能
与官方标准精度发行包混用。
