# LILYGO UI Files

[English](README.md) | 简体中文

`Files` 是一个独立的 LILYGO UI 文件浏览器。它是一个顶层 CMake 项目，并遵循
`lilygo-ui-template` 的公共接口：

- 软件包和可执行文件：`lilygo-ui-files`
- 应用 ID：`cc.lilygo.ui.Files`
- AppKit 依赖：`find_package(LilyGoUI CONFIG REQUIRED)`

应用身份、发布元数据、权限、兼容性、构建 preset 和部署默认值仅在 `lpm.toml`
中维护。CMake 通过 LPM 校验该文件，并据此生成桌面集成元数据和软件包配置。
`publish.json` 是 LPM 的临时输出，不是项目配置文件。

应用只在一个已配置的文件系统根目录内运行。它支持目录导航、当前目录搜索、
只读文本预览和新建文件夹。目前不向用户提供复制、重命名、移动、压缩、删除、
选择项目、修改排序方式或切换视图等操作。

Files 页面采用 C++17 的 `View -> ViewModel -> Model` 依赖方向。`FilesModel`
负责文件系统访问和领域规则且不依赖 LVGL；`FilesViewModel` 通过 LVGL Subject
发布展示状态；`FilesView` 创建响应式 LVGL 界面并转发命令。界面直接使用 LVGL
实现，不使用生成式设计源码。

项目特定的架构和 UI 要求记录在
[`docs/00-overview.md`](docs/00-overview.md) 和
[`docs/01-user-interface.md`](docs/01-user-interface.md) 中。

## 准备 AppKit

开发前先初始化仓库锁定的 AppKit submodule：

```sh
git submodule update --init --recursive
```

默认 preset 将 `LilyGoUI_DIR` 指向 `third_party/cm0-appkit`。应用通过
`find_package(LilyGoUI CONFIG REQUIRED)` 加载源码 SDK 配置，由 SDK 构建
AppKit/LVGL 静态库并链接进应用。应用 CMake 不得枚举 SDK 私有源文件或引用
其私有头文件。AppKit 代码更新后需要更新子模块版本并重新构建应用。

`lilygo-ui-appkit-dev` 0.1.0 或更新版本同时提供源码 SDK、公共字体和许可证，
应用不重复打包字体。`lpm.toml` 的 `min_appkit_version` 指定该软件包的最低版本。
AppKit/LVGL 仍静态链接进每个应用，该软件包不包含 AppKit/LVGL 动态库。
交叉编译 sysroot 只需 BSP 和系统开发依赖，不需要预装 AppKit SDK。
主机预览使用源码 SDK 中的字体资源。

可通过以下方式选择独立的 AppKit 源码检出：

```sh
cmake --preset host-simulator -DLilyGoUI_DIR=/path/to/appkit
```

## 主机模拟器

```sh
lpm start
```

也可以直接使用稳定的 CMake 接口：

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
./build/host-simulator/lilygo-ui-files
```

主机模拟器构建默认使用 `simulated-home/` 作为文件根目录，调试期间的文件系统
修改会保留在项目内。可以在该目录中添加或编辑文件来测试界面，也可以显式选择
其他目录：

```sh
./build/host-simulator/lilygo-ui-files --root=/tmp/files-demo
```

启动环境也可以通过 `LILYGO_UI_FILES_ROOT` 覆盖根目录。还可以在
`$XDG_CONFIG_HOME/lilygo/ui/files.conf` 中使用
`files.root=/absolute/path` 持久设置根目录；如果未设置 `XDG_CONFIG_HOME`，则读取
`$HOME/.config/lilygo/ui/files.conf`。系统级配置文件为
`/etc/lilygo/ui/files.conf`。配置优先级依次为：命令行、环境变量、用户配置、
系统配置和运行时默认值。配置值必须是绝对路径。

UI 必须验证的视口为 `568x1232` 竖屏和 `1232x568` 横屏。测试设计还使用
`320x568` 紧凑视口和 `1024x768` 大视口作为额外边界检查；完整验收矩阵见 UI
文档。

## 字体

应用中的所有文本都使用 `lilygo_ui_font_get()`。Inter、Source Han Sans CN 和
Font Awesome 由共享的 `lilygo-ui-appkit-dev` SDK 和字体软件包提供，本应用不会重复
打包字体文件。

## 设备软件包

```sh
lpm pack
```

等价的底层 CMake 和 CPack 命令为：

```sh
cmake --preset cm0-cross
cmake --build --preset cm0-cross --parallel
cpack --config build/cm0-cross/CPackConfig.cmake -B dist
```

Debian 打包规则会安装可执行文件、Launcher manifest、desktop 文件、AppStream
元数据、应用图标、默认配置和 AppKit 字体声明。版本、供应商、主页、目标架构和
最低 AppKit 依赖均从 `lpm.toml` 派生。

在目标设备上，软件包提供的系统配置会将文件根目录设为 `/home/pi`。可以通过
`--root`、`LILYGO_UI_FILES_ROOT` 或上述用户/系统配置文件覆盖该值。

## 源码结构

- `src/main.cpp`：解析根目录配置，然后进入 AppKit 运行时
- `src/app.cpp` 和 `src/app.hpp`：应用生命周期和 Files 页面装配
- `src/app_config.cpp` 和 `src/app_config.hpp`：配置文件解析
- `src/pages/files/files_model.cpp`：文件系统访问、状态和领域规则
- `src/pages/files/files_view_model.cpp`：展示 DTO、命令和 Subject
- `src/pages/files/files_view.cpp`：响应式 LVGL widget 和事件转发
- `src/components/`：共享语义样式和 Subject 生命周期辅助工具
- `simulated-home/`：用于交互调试的主机端文件系统 fixture
- `data/`：通用 Launcher、desktop 和 AppStream 模板以及默认配置
- `tests/`：Model、ViewModel/Subject 和无头响应式渲染测试

打包时会安装 128x128 RGBA 格式的 `assets/app-icon.png`。
