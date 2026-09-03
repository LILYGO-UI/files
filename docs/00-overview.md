# LILYGO UI Files 项目总览

`lilygo-ui-files` 是一个独立的 LILYGO UI 文件浏览应用。它作为顶层 CMake
项目配置、构建、测试、安装和打包，并且只通过
`find_package(LilyGoUI CONFIG REQUIRED)` 使用平台 SDK。

本文描述本应用的产品范围、工程边界和验收要求。通用 UI 规则及 Files 页面
的具体交互约束见 [01-user-interface.md](01-user-interface.md)。

## 产品范围

应用只操作启动时确定的文件根目录，并提供以下用户可见能力：

- 浏览根目录及其子目录；
- 在当前目录中按名称搜索文件和文件夹；
- 只读预览受支持的文本文件；
- 在当前目录中新建文件夹；
- 显示当前根目录所在文件系统的容量和已用空间。

搜索不递归进入子目录。文本预览最多读取 8 KiB，超出部分以截断提示结尾。
非文本文件可以出现在列表中，但不提供内容查看器。符号链接不会被当作目录进入，
也不会被作为文本文件读取；页面导航不得越过配置的根目录。

复制、重命名、移动、压缩、删除、项目选择、排序切换和视图切换目前不属于可用
功能。界面不得显示会让用户误以为这些操作可用的按钮、菜单项或切换控件。

## 稳定身份

项目的公开身份为：

```text
包名和可执行文件  lilygo-ui-files
应用 ID            cc.lilygo.ui.Files
Desktop/AppStream  cc.lilygo.ui.Files
```

身份、版本、描述、许可证、资产、权限、兼容性、Launcher 顺序、构建 preset 和
部署默认值只在 `lpm.toml` 中维护。`data/*.in` 是通用输出模板，不得重复写入
应用专属值。`publish.json` 是 LPM 的临时派生数据，不得手工编辑、提交或作为
构建输入。

## 文件根目录

根目录按下列优先级解析，命中后不再继续查找：

1. 命令行 `--root PATH` 或 `--root=PATH`；
2. 环境变量 `LILYGO_UI_FILES_ROOT`；
3. 用户配置 `$XDG_CONFIG_HOME/lilygo/ui/files.conf`，未设置
   `XDG_CONFIG_HOME` 时使用 `$HOME/.config/lilygo/ui/files.conf`；
4. 系统配置 `/etc/lilygo/ui/files.conf`；
5. 主机模拟器使用仓库内 `simulated-home/`，其他环境使用当前用户主目录，
   无法取得主目录时回退到 `/`。

配置文件格式为：

```ini
files.root=/absolute/path
```

配置值必须是绝对路径。设备包提供的系统默认值为 `/home/pi`。`--root` 是数据
边界配置，不是界面展示模式；应用不得提供 phone、desktop、横屏或竖屏模式开关。

## 架构

应用源码和测试使用 C++17 或更新标准，仅在 AppKit 生命周期入口保留 C ABI。
Files 页面遵循固定依赖方向：

```text
FilesView -> FilesViewModel -> FilesModel
```

- `FilesModel` 负责目录扫描、路径边界、搜索、文本读取、新建文件夹和错误结果，
  不依赖 LVGL，可在未调用 `lv_init()` 时测试。
- `FilesViewModel` 把 Model 数据转换为列表项、容量摘要等展示对象，通过 LVGL
  Subject 发布状态，并暴露用户命令。
- `FilesView` 只创建和布局 LVGL 对象、绑定状态、转发事件，不直接访问文件系统，
  也不直接修改 Model。
- `app.cpp` 管理 AppKit 生命周期以及 Model、ViewModel、View 的创建和销毁顺序。

数据流保持单向：

```text
用户输入 -> View 事件 -> ViewModel 命令 -> Model 更新
         -> Subject -> View observer/绑定 -> LVGL 渲染
```

ViewModel 必须比订阅它的 View 存活更久。Subject 使用
`src/components/lv_subject.hpp` 中的 RAII 封装，不复制或移动底层
`lv_subject_t`。View 的 observer 应绑定到所属 LVGL 对象；异步刷新不得在当前
事件或 observer 回调中同步销毁触发回调的对象。

## 源码结构

```text
src/
├── main.cpp                         # 根目录解析和 AppKit 运行时入口
├── app.cpp/.hpp                     # 应用生命周期和页面装配
├── app_config.cpp/.hpp              # 配置文件读取
├── pages/files/
│   ├── files_model.cpp/.hpp         # 文件系统模型和领域规则
│   ├── files_view_model.cpp/.hpp    # 展示状态、Subject 和命令
│   └── files_view.cpp/.hpp          # LVGL 页面、布局和事件转发
└── components/                      # 共享样式、组件和 Subject 生命周期辅助
```

生产代码和公共集成只放在 `src/`、`data/`、`cmake/` 和 `tests/`。界面直接使用
LVGL 构建，不引入 EEZ 或其他生成式设计源码树。只有确实被多个页面共享的领域
对象才应移入 `src/domain/`。

## 构建入口

首次使用先初始化 AppKit submodule：

```sh
git submodule update --init --recursive
```

LPM 是日常入口，CMake presets 是稳定的底层入口：

| 目标 | LPM | CMake/CTest/CPack |
| --- | --- | --- |
| 启动主机模拟器 | `lpm start` | `cmake --preset host-simulator` 后构建并运行可执行文件 |
| 主机测试 | `lpm test` | `ctest --preset host-simulator` |
| 交叉构建和打包 | `lpm pack` | `cmake --preset cm0-cross` 后构建并运行 CPack |
| 部署设备 | `lpm deploy` | 无直接 CMake 等价入口 |

不得依赖 `build/` 内部目录作为公共接口，也不得在源码目录内构建。

## 验收要求

每次改动至少应完成：

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
```

Model 测试不得初始化 LVGL；ViewModel 测试应验证命令结果和 Subject 发布；渲染
测试应通过语义对象名称定位关键控件，而不是依赖子对象下标。

修改 UI 时必须检查 `568x1232` 竖屏和 `1232x568` 横屏中的所有可达页面，并
验证运行时尺寸变化后当前目录、搜索文字、预览内容、焦点和滚动状态不丢失。
`320x568` 和 `1024x768` 可作为额外的紧凑与大尺寸回归视口。长文件名、长路径、
空目录、错误状态以及超过 8 KiB 的文本预览都必须有覆盖。

修改 SDK、工具链、安装、元数据或打包逻辑时，还必须执行 `cm0-cross` 构建和
Debian 包内容检查。文档中的命令是要求和入口，不代表当前工作树已经完成验证。
