# GDPP

GDPP 是面向 Godot 4.4～4.7 的 GDScript AOT 编译器与编辑器插件。它使用 C++17、CMake 和作为
`third/` 子模块固定的 godot-cpp，把项目 GDScript 转译、编译并链接为 GDExtension 动态库；严格
二进制导出只交付游戏资源、项目原生库、运行描述符以及项目本来依赖的第三方插件，不交付
GDScript 源码、编译器、SDK 或生成 C++。

当前开发版本为 1.7.0。编译器前端始终跟进最新版稳定 GDScript，Godot 4.4～4.7 是独立的目标
API/SDK/ABI 选择，而不是四套语法方言。项目仍处于按公开路线图推进的商业化开发阶段；支持矩阵
只以自动门禁和实机报告中列明的范围为准。

## 第三方 GDExtension 继承

现有项目可以继续直接编写：

```gdscript
extends VendorNode

func compute(value: int) -> int:
    return super.native_compute(value) + bonus
```

客户无需修改脚本、场景、资源、Autoload 或供应商 GDExtension 源码。GDPP 不注册 Godot 尚不
支持的跨动态库 C++ 子类，而是通过 `ScriptLanguageExtension`、`ScriptExtension` 和
`ScriptInstance` 把生成行为附着到供应商拥有的真实 Object。公开原生成员从 ClassDB 自动采集；
外部 `super` 使用精确 MethodBind 兼容哈希通过 GDExtension ABI 调用。

供应商插件必须存在对应目标平台动态库，并在项目运行时之前完成 ClassDB 注册。无 Godot 进程的
CLI/交叉构建可提供机器生成的 `gdpp_bridge.json`。私有 C++ 方法、未注册类型或缺少精确 ABI 的
调用会失败关闭，不会猜测签名。完整契约和零改动边界见
[第三方 GDExtension 类型兼容](docs/GDEXTENSION_BRIDGE.md)。

## 开发构建

```sh
git submodule update --init --recursive
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev
```

构建 Godot compiler 插件使用 `plugin` preset；正式发行使用 `release` preset。插件产物直接进入
`example/addons/gdpp/`，启用后可从 Godot 的 **项目 > 工具 > Build GDPP Native Library** 构建
当前项目。客户项目构建只要求受支持的系统 C++ 工具链；godot-cpp 生成器和 GDPP 自身构建工具不
进入客户导出包。

## 工程与交付文档

- [架构与不变量](docs/ARCHITECTURE.md)
- [项目构建和自动导出](docs/PROJECT_BUILD.md)
- [GDScript 兼容性与真实边界](docs/COMPATIBILITY.md)
- [商业交付模型](docs/COMMERCIAL_DELIVERY.md)
- [跨平台实测报告](docs/PLATFORM_TEST_REPORT.md)
- [路线图](docs/ROADMAP.md)
- [版本变更（中文）](CHANGELOG-ZH.md) / [English changelog](CHANGELOG.md)
