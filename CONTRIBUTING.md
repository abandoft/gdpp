# 参与开发

先初始化依赖，再使用预设构建，确保所有产物都位于 `build/`：

```sh
git submodule update --init --recursive
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev
```

使用 `cmake --preset plugin` 构建 GDExtension、示例 Godot 项目和生成代码冒烟目标。
新增语言行为必须提供对应的词法、语法或语义测试；涉及代码生成时，还必须提供生成 C++
编译测试或 Godot 行为测试。编译器核心不得依赖 Godot 类型，Godot 相关代码只放在运行时
与 `src/godot/` 边界内；是否进入核心由 CMake target 的源清单和链接关系决定。

提交前执行：

```sh
find include src test -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0 \
  | xargs -0 clang-format --dry-run --Werror
cmake --build --preset plugin --parallel
ctest --preset plugin
```
