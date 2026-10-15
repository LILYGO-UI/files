# LILYGO UI Files 项目总览

`lilygo-ui-files` 是一个独立的 LILYGO UI 文件浏览应用。它作为顶层 CMake
项目配置、构建、测试、安装和打包，并且只通过
`find_package(LilyGoUI CONFIG REQUIRED)` 使用平台 SDK。

本文描述本应用的产品范围、工程边界和验收要求。通用 UI 规则及 Files 页面
的具体交互约束见 [01-user-interface.md](01-user-interface.md)。

## 产品范围

应用只操作启动时确定的文件根目录，并提供以下用户可见能力：

- 浏览根目录及其子目录，可切换图标视图和列表视图；
- 按名称、种类、日期或大小排序，并可切换升序和降序；
- 进入选择模式选择项目，支持全选和取消全选，并通过底部工具栏对所选项目执行复制
  （创建副本）、移动和删除，通过选择菜单执行拷贝、用所选项目新建文件夹和压缩；
- 在当前目录中按名称搜索文件和文件夹；
- 只读预览受支持的文本文件；
- 在当前目录中新建文件夹；
- 从“更多”菜单打开可滚动的 Help 页面，查看文件和文件夹操作说明；
- 长按文件或文件夹弹出上下文菜单，对该项目执行拷贝、移动、显示简介、重新命名、
  压缩、复制（创建副本）、用 1 个项目新建文件夹和删除；
- 在更多操作菜单中把已拷贝的一个或多个项目粘贴到当前目录。

搜索不递归进入子目录。文本预览最多读取 8 KiB，超出部分以截断提示结尾。
非文本文件可以出现在文件网格中，但不提供内容查看器。符号链接不会被当作目录进入，
也不会被作为文本文件读取；页面导航不得越过配置的根目录。

上下文菜单只作用于长按的单个项目，选择工具栏和选择菜单作用于全部所选项目；简介和
重新命名只针对单个项目，不对所选项目提供。共享、标签（自定义文件夹与标签）和收藏
不属于产品范围，界面不得显示这些入口。

项目操作遵循以下规则：

- 所有操作只在配置的根目录内进行，不跟随符号链接：拷贝、复制和压缩保存链接本身，
  删除只移除链接，不影响其目标；
- 同名冲突时，粘贴、复制、压缩和新建文件夹按 iOS 方式追加编号（`名称 2.txt`、
  `New Folder 2`）；重新命名和移动遇到同名项目时报错，不覆盖已有项目；
- 复制和粘贴生成的文件保留原文件的修改日期；文件夹连同隐藏内容一起复制；
- 压缩在同一目录生成 `名称.zip`，同时压缩多个项目时生成 `Archive.zip`，使用 DEFLATE
  压缩，不缩小的数据按存储方式写入；不支持 ZIP64，单个文件或整个归档超过 4 GiB 时
  报错，失败时删除不完整的归档；
- 移动跨越文件系统时先复制再删除原项目；文件夹不能移动或粘贴到它自身或其子目录；
  移动和粘贴多个项目时先检查全部项目，任一项目被拒绝时不改变任何项目；
- 对多个项目的操作逐项执行，中途失败时停止并显示已经完成的结果，未完成的项目保持
  选中或留在当前提示中；用所选项目新建文件夹失败时把已移入的项目移回原处；
- 删除是永久删除（没有“最近删除”），必须先经确认提示框；
- 简介中的文件夹大小和项目数量递归统计可见内容，最多统计 10000 个项目；
- 操作在界面线程同步执行，很大的目录树会让界面在操作期间暂停响应。

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

配置值必须是绝对路径。设备包不设置 `files.root`，默认使用当前运行用户的主目录，
不假定设备存在 `pi` 用户。已有安装若仍配置 `files.root=/home/pi`，需删除或注释
该行才能恢复默认行为。`--root` 是数据边界配置，不是界面展示模式；应用不得提供
phone、desktop、横屏或竖屏模式开关。

## 架构

应用源码和测试使用 C++17 或更新标准，仅在 AppKit 生命周期入口保留 C ABI。
Files 页面遵循固定依赖方向：

```text
FilesView -> FilesViewModel -> FilesModel
```

- `FilesModel` 负责目录扫描（含修改时间和子目录可见条目数量）、排序、显示方式、
  选择状态、路径边界、搜索、文本读取、新建文件夹、上下文菜单和选择工具栏的项目
  操作、剪贴板、
  移动目标浏览、简介统计和错误结果，不依赖 LVGL，可在未调用 `lv_init()` 时测试。
  ZIP 归档和 DEFLATE 编码由同层的 `files_archive` 实现，不依赖 zlib 或其他系统库。
- `FilesViewModel` 把 Model 数据转换为网格项（名称、修改日期、大小或条目数量、
  类型标签）、预览信息等展示对象，通过 LVGL Subject 发布状态，并暴露用户命令。
- `FilesView` 只创建和布局 LVGL 对象、绑定状态、转发事件，不直接访问文件系统，
  也不直接修改 Model。
- `HelpView` 是不依赖 Model 的静态 LVGL 页面，只负责展示操作说明并通过回调返回 Files 页面。
- `app.cpp` 管理 AppKit 生命周期以及 Model、ViewModel、FilesView、HelpView 的创建和销毁顺序。

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
│   ├── files_archive.cpp/.hpp       # 压缩使用的 ZIP 写入和 DEFLATE 编码
│   ├── files_view_model.cpp/.hpp    # 展示状态、Subject 和命令
│   ├── files_view.cpp/.hpp          # LVGL 页面、布局和事件转发
│   └── files_icons.cpp/.hpp         # 文件夹和文档图标的绘制
├── pages/help/
│   ├── help_view.cpp/.hpp            # 静态、可滚动的操作帮助页
│   └── help_content.hpp              # 帮助页文案分组
└── components/                      # 共享样式、组件和 Subject 生命周期辅助
```

生产代码和公共集成只放在 `src/`、`data/`、`cmake/` 和 `tests/`。界面直接使用
LVGL 构建，不引入 EEZ 或其他生成式设计源码树。只有确实被多个页面共享的领域
对象才应移入 `src/domain/`。

## 构建入口

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
空目录、错误状态以及超过 8 KiB 的文本预览都必须有覆盖。项目操作的 Model 测试
必须验证文件系统结果，压缩测试必须解出归档内容并校验 CRC。

修改 SDK、工具链、安装、元数据或打包逻辑时，还必须执行 `cm0-cross` 构建和
Debian 包内容检查。文档中的命令是要求和入口，不代表当前工作树已经完成验证。
