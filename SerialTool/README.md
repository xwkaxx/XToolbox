# SerialTool 串口助手

SerialTool 是 XToolbox 解决方案中的独立 Windows 串口工具，使用 C++、Dear ImGui 和 DirectX 11 实现。支持串口连接、原始字节收发、定时发送及收发记录显示，可直接运行 `SerialTool.exe`，也可由 XToolbox 启动器打开。

本文说明当前源码结构、已实现功能、构建测试方式及维护边界。状态更新于 **2026-10-01**。

## 当前状况

已完成通信、应用流程、数据模型和 UI 的职责拆分，并将对应的 `.cpp` 与 `.h` 放在同一功能目录中。`ui/serial_ui.cpp` 负责资源生命周期、主题与面板组合；串口操作、工作线程和业务流程分别由通信会话及控制器管理。

最近一次目录整理后的验证结果：

- 主程序 `Debug | x64` 编译、链接通过。
- 独立测试项目编译通过，全部 **12 组回归测试**通过。
- 本轮重构尚未进行真实串口设备收发及实际界面交互验证。
- `Release | x64` 和 Win32 配置未在本轮验证；目前以 `Debug | x64` 为开发基线。

## 已实现功能

| 功能 | 当前行为 |
| --- | --- |
| 设备发现 | 展开端口列表时刷新，按 COM 编号排序；查询设备描述、制造商和实例标识，不占用串口 |
| 连接配置 | 预设及自定义波特率；5～8 数据位；None、Odd、Even 校验；1、1.5、2 停止位；默认 115200、8N1 |
| 数据发送 | HEX 原始字节或 UTF-8 字符模式；切换格式时保留原始字节；支持单次发送 |
| 发送后缀 | 无追加、LF、CR、LFCR、CRLF、CRC16/Modbus；CRC 低字节在前 |
| 连续发送 | 设置发送后延时及总次数；`-1` 表示无限发送；显示成功次数并支持停止 |
| 接收与显示 | HEX、UTF-8 显示切换；区分收发颜色；可选毫秒时间戳、Rx/Tx 显示开关及字号缩放 |
| 输出交互 | 自动折行、文字选择、复制、全选和发送区高度拖动 |
| 清理操作 | 清原始缓存并重置计数，或只清显示文字；两种操作分别保留不同数据 |
| 配置保存 | 保存所选端口、最后一次有效提交内容、发送选项和显示偏好；兼容旧 TXT 配置迁移 |
| 窗口 | 自定义标题栏、置顶、最小化、最大化、关闭及窗口边缘缩放 |

设备实例标识不保证是硬件序列号。波特率等参数能否实际应用取决于设备和驱动。

## 目录结构

下面的 `.cpp/.h` 表示同名实现与头文件。

```text
SerialTool/
├─ main.cpp                         # Win32 窗口、DX11、ImGui 初始化及消息循环
├─ communication/
│  ├─ serial_port.cpp/.h             # Windows 串口句柄、配置和同步读写
│  ├─ serial_devices.cpp/.h          # 端口枚举与设备信息查询
│  └─ serial_session.cpp/.h          # 工作线程、队列、定时发送及关闭顺序
├─ application/
│  ├─ serial_controller.cpp/.h       # 连接、发送、结果处理和模块协调
│  └─ serial_settings.cpp/.h         # JSON 配置读写、校验与旧配置迁移
├─ models/
│  ├─ serial_codec.cpp/.h            # HEX、文本、转义及发送后缀转换
│  ├─ serial_send_editor.cpp/.h      # 输入缓冲、字节映射和有效提交内容
│  └─ serial_history.cpp/.h          # 历史记录、合并、裁剪及收发计数
├─ ui/
│  ├─ serial_ui.cpp/.h               # 界面入口、资源管理、主题和整体布局
│  ├─ serial_connection_panel.cpp/.h # 连接入口、导航及串口配置面板
│  ├─ serial_send_panel.cpp/.h       # 发送输入、选项和进度绘制
│  ├─ serial_output_panel.cpp/.h     # 输出工具栏、折行、选择及复制
│  ├─ serial_ui_widgets.cpp/.h       # 公共绘制控件及图标资源集合
│  ├─ serial_window.cpp/.h           # 标题栏绘制与 Win32 命中测试
│  └─ image_texture.cpp/.h           # 嵌入图片解码与 DX11 纹理加载
├─ tests/
│  ├─ serial_models_tests.cpp        # 编辑器和历史模型测试、测试入口
│  ├─ serial_session_tests.cpp       # 模拟传输及控制器流程测试
│  └─ serial_models_tests.vcxproj    # 独立控制台测试项目
├─ assets/icons/                    # 图片资源源文件
├─ resource.h                       # 资源 ID
├─ SerialTool.rc                    # 嵌入资源定义
├─ SerialTool.vcxproj                # 主程序构建配置
└─ SerialTool.vcxproj.filters        # Visual Studio 中的功能分组
```

仓库级依赖位于上级目录：`../ImGui/` 为 ImGui 静态库项目，`../ThirdParty/imgui/` 为子模块源码，`../ThirdParty/nlohmann/` 提供 JSON 库。构建时从 `../XToolbox/assets/fonts/` 复制字体及许可证到程序输出目录。

## 模块协作与线程边界

```text
main.cpp
  ├─ serial_window：窗口消息处理
  └─ serial_ui：生命周期、布局和面板组合
       ├─ connection / send / output 面板
       ├─ serial_ui_widgets：公共控件与图标
       └─ serial_controller：应用流程
            ├─ serial_devices：设备发现
            ├─ serial_session → serial_port：通信调度与底层读写
            ├─ serial_send_editor / serial_codec：发送数据处理
            ├─ serial_history：结果记录与统计
            └─ serial_settings：配置持久化
```

这是运行职责示意，并非完整的头文件依赖图。

- **主线程**调用控制器和会话的公开操作，处理 UI、编辑器、历史记录及配置读写。
- **唯一收发线程**执行同步 `Read`、`Write`，处理普通发送队列和定时任务，通过有界队列交付记录与错误通知。
- `SerialSession::Poll()` 批量取出事件，控制器将实际收发计数和记录交给历史模型。工作线程不访问 ImGui 或 UI 状态。
- 关闭连接时，先设置停止标记、取消同步 I/O 并等待线程退出，再关闭串口句柄。
- 面板显式借用控制器和只读图标集合；主界面统一加载、释放纹理，各面板自行保存动画、选择等显示状态。

## 需要保持的行为

1. 部分写入只统计实际写出的字节，失败后不自动重发整段数据。写入成功仅表示本机写入完成，不代表对端已解析或执行。
2. 连续发送使用启动时的数据、模式和后缀快照；修改输入或清空输入框不会改变正在进行的一轮发送。停止后，已开始的一次写入允许完成。
3. 时间戳和 Rx/Tx 可见性在记录生成时确定，切换开关不追溯修改历史。输出 HEX/文本模式可重新格式化仍保有原始字节的可见记录。
4. 历史记录最多保留 **256 KiB 原始字节、2048 条记录**；工作线程的待消费记录也有容量限制。裁剪旧显示数据不会减少实际收发计数。
5. **清原始缓存**保留当前文字并重置计数，旧文字不再切换格式；**清显示文字**隐藏已有记录，保留缓存和计数。
6. 一条收发记录不等于一个完整协议帧。同一毫秒且属性相容的记录可以合并，但没有协议分帧或报文解析。

## 构建与运行

开发环境使用 Windows、支持 `.slnx` 的 Visual Studio、C++ 桌面开发工作负载、MSVC **v145** 和 Windows 10 SDK。x64 项目使用 **C++17**。

### Visual Studio

1. 确认仓库的 `ThirdParty/imgui` 子模块已初始化，JSON 头文件和字体资源存在。
2. 打开仓库根目录的 `XToolbox.slnx`。
3. 选择 `Debug | x64`，将 **SerialTool** 设为启动项目。
4. 生成并运行。若使用 XToolbox 启动器，需先生成 SerialTool，确保两个 EXE 位于同一输出目录。

输出位置相对于仓库根目录：

```text
Build/bin/x64/Debug/SerialTool.exe
Build/bin/x64/Debug/assets/fonts/SourceHanSansCN-Regular.otf
Build/obj/SerialTool/x64/Debug/
```

图标由 `.rc` 嵌入 EXE；字体作为外部文件随构建复制，运行时需要保留 `assets/fonts/`。

### 命令行

在 Visual Studio 的 **Developer PowerShell** 中执行，工作目录为仓库根目录：

```powershell
Set-Location D:\xwkLearning\XToolbox
$repoRoot = (Get-Location).Path + '\'
msbuild .\SerialTool\SerialTool.vcxproj /p:Configuration=Debug /p:Platform=x64 "/p:SolutionDir=$repoRoot"
.\Build\bin\x64\Debug\SerialTool.exe
```

如果缺少 ImGui 子模块，可在仓库根目录执行 `git submodule update --init --recursive`。当前构建基线为 x64；Win32 项目配置与 x64 不完全相同，不应直接视为已验证的平台。

## 配置与容量

配置文件为 **EXE 所在目录下的 `config/preferences.json`**，不取决于启动时的工作目录。文件不存在时使用默认值，尝试迁移旧 TXT 配置并创建 JSON；已有 JSON 无效时会报告错误，不用旧 TXT 覆盖它。

旧配置迁移依次检查 EXE 旁的 `config/preferences.txt`，以及 `%LOCALAPPDATA%/XToolbox/SerialTool/preferences.txt`。保存时先写临时文件，再替换正式文件。

当前保存的字段包括端口、最后一次有效提交内容、发送模式和后缀、连续发送参数，以及输出模式、时间戳、Rx/Tx 开关和字号。**波特率、校验位、数据位、停止位和窗口布局目前不持久化**。启动时不会自动连接或开始发送。

发送编辑缓冲为 **2048 字节，包含结尾 NUL**；格式转换后超出容量时保留原内容与模式。普通发送队列最多允许 64 个待处理任务。连续发送延时可设为 1～86400000 ms，总次数为 1～1000000 或 `-1`。

连续发送延时从上一帧写入完成后计算，还受同步读取、驱动和线程调度影响，不提供硬实时的周期保证。

## 回归测试

测试项目位于 `tests/serial_models_tests.vcxproj`，当前没有加入主解决方案，需单独构建。以下命令同样在仓库根目录的 Developer PowerShell 执行：

```powershell
msbuild .\SerialTool\tests\serial_models_tests.vcxproj /p:Configuration=Debug /p:Platform=x64
.\Build\tests\x64\Debug\serial_models_tests.exe
```

当前 12 组测试覆盖：

- 编辑器：全部 256 种字节的格式往返、编辑时保留未改动字节、无效转换及草稿与有效提交的区别。
- 历史模型：合并边界、容量裁剪和计数、两种清空操作。
- 通信会话：收发与记录标记、定时发送及任务代次、部分写入和故障重连、队列上限及关闭顺序、接收缓存上限。
- 控制器：参数映射、发送快照、CRC 追加和清理流程。

通信测试使用模拟传输，不需要真实串口。控制器测试会在测试 EXE 旁生成配置文件，位置属于 `Build/tests/`，与主程序的配置分开。测试不验证设备驱动、真实线路、屏幕绘制和鼠标键盘交互。

## 当前限制与维护约定

当前为单串口会话，同步读写由一个工作线程串行处理。读写失败后关闭连接，需要用户手动重新连接；尚未实现自动重连、硬件或软件流控开关、DTR/RTS 手动控制、文件发送、日志导出及协议解析。CRC16/Modbus 目前仅用于追加发送校验，不代表完整的 Modbus 协议支持。

后续修改按职责放置：通信和线程逻辑进入 `communication/`，应用操作进入 `application/`，字节处理与历史规则进入 `models/`，布局和绘制进入 `ui/`。不要在 UI 中直接访问串口句柄、队列锁或工作线程。

新增、移动或删除源文件时，同步更新 `SerialTool.vcxproj`、`SerialTool.vcxproj.filters`、相关 `#include` 及测试项目源码路径。修改通信或数据规则后运行回归测试；修改交互、字体或布局后，还需进行实际界面检查。构建产物统一放在仓库的 `Build/` 下。
