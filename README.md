# XToolbox

一个正在开发中的 Windows C++ 工具箱。  
A Windows toolbox built with C++, currently under development.

[中文](#中文) | [English](#english)

## 中文

### 项目简介

XToolbox 使用 C++、Dear ImGui 和 DirectX 11 构建，目标是提供统一的工具选择入口，逐步接入串口等实用工具。

名称中的 X 是作者的个人标志，Toolbox 代表包含多个工具的工具箱。

项目目前处于基础框架搭建阶段。

### 当前进度

- 已建立 Visual Studio 解决方案。
- 已通过 Git Submodule 接入 Dear ImGui。
- 已配置 ImGui 静态库及 Win32、DirectX 11 后端。
- 已能够编译运行并显示 ImGui 示例界面。
- 工具选择界面和具体工具功能尚未实现。

### 开发环境

- Windows x64
- 支持 `.slnx` 解决方案格式的 Visual Studio
- Visual Studio“使用 C++ 的桌面开发”工作负载
- MSVC v145 工具集
- Windows SDK 10.0 系列
- C++17
- Git

当前项目配置使用 v145 工具集。使用其他工具集时，可能需要重新定向项目并验证兼容性。

### 获取源码

克隆仓库并获取子模块：

```powershell
git clone --recurse-submodules https://github.com/xwkaxx/XToolbox.git
```

如果已经普通克隆，在仓库根目录执行：

```powershell
git submodule update --init --recursive
```

主仓库记录了所使用的 ImGui 具体提交。子模块初始化需要能够访问其远程仓库。

直接下载主仓库 ZIP 不包含完整的子模块源码。

### 编译运行

1. 使用 Visual Studio 打开 `XToolbox.slnx`。
2. 选择 `Debug | x64`。
3. 将 `XToolbox` 设置为启动项目。
4. 执行“生成解决方案”。
5. 按 F5 运行。

程序输出位置：

```text
Build/bin/x64/Debug/XToolbox.exe
```

当前启动后显示 ImGui 示例界面，不会自动创建控制台窗口。

### 目录结构

```text
XToolbox/
├─ XToolbox.slnx       Visual Studio 解决方案
├─ main.cpp           程序入口、窗口及渲染循环
├─ XToolbox/          主程序项目文件
├─ ImGui/             ImGui 静态库项目文件
├─ ThirdParty/
│  └─ imgui/          Dear ImGui 子模块
├─ Build/             编译产物，不纳入版本管理
├─ .gitmodules        子模块配置
├─ .gitignore         Git 忽略规则
└─ README.md          项目说明
```

### 开发计划

- 实现工具选择界面。
- 接入独立的串口工具。
- 按实际需求逐步增加其他工具。
- 补充使用说明和界面截图。

以上为开发计划，不代表当前已提供的功能。

### 问题反馈与贡献

欢迎通过 Issues 报告问题或提出建议。

报告编译问题时，请提供开发环境、构建配置、复现步骤以及第一条编译或链接错误。

涉及较大改动时，建议先通过 Issue 讨论范围和实现方向。

### 第三方依赖

[Dear ImGui](https://github.com/ocornut/imgui) 使用 MIT 许可证。
许可证原文见 [ThirdParty/imgui/LICENSE.txt](ThirdParty/imgui/LICENSE.txt)。

第三方依赖的许可证不代表 XToolbox 自身的许可证。

## English

### About

XToolbox is a Windows application built with C++, Dear ImGui, and DirectX 11.
It aims to provide a shared launcher for practical utilities, starting with a serial communication tool.

The X represents the author's personal identity, while Toolbox describes the collection of utilities.

The project is currently in its initial setup phase.

### Current Status

- Visual Studio solution created.
- Dear ImGui integrated as a Git submodule.
- ImGui static library and Win32/DirectX 11 backends configured.
- Application builds and displays the ImGui example interface.
- The tool selection interface and individual tools are not yet implemented.

### Development Requirements

- Windows x64
- Visual Studio with support for `.slnx` solution files
- The “Desktop development with C++” workload
- MSVC v145 toolset
- Windows SDK from the 10.0 series
- C++17
- Git

The project currently targets the v145 toolset. Other toolsets may require project retargeting and compatibility testing.

### Getting the Source

Clone the repository with its submodules:

```powershell
git clone --recurse-submodules https://github.com/xwkaxx/XToolbox.git
```

If you have already cloned the repository, run the following from its root directory:

```powershell
git submodule update --init --recursive
```

The main repository records the specific ImGui commit used by the project.
Initializing the submodule requires access to its remote repository.

Downloading the main repository as a ZIP does not include the complete submodule source.

### Building and Running

1. Open `XToolbox.slnx` in Visual Studio.
2. Select `Debug | x64`.
3. Set `XToolbox` as the startup project.
4. Build the solution.
5. Press F5 to run.

The executable is generated at:

```text
Build/bin/x64/Debug/XToolbox.exe
```

The application currently displays the ImGui example interface without automatically opening a console window.

### Project Structure

```text
XToolbox/
├─ XToolbox.slnx       Visual Studio solution
├─ main.cpp           Application entry, window, and rendering loop
├─ XToolbox/          Main application project files
├─ ImGui/             ImGui static library project files
├─ ThirdParty/
│  └─ imgui/          Dear ImGui submodule
├─ Build/             Build artifacts, excluded from version control
├─ .gitmodules        Submodule configuration
├─ .gitignore         Git ignore rules
└─ README.md          Project documentation
```

### Roadmap

- Implement the tool selection interface.
- Integrate a standalone serial communication tool.
- Add other utilities as needed.
- Provide usage documentation and screenshots.

These are planned features and are not currently available.

### Feedback and Contributions

Bug reports and suggestions through Issues are welcome.

For build issues, include your development environment, build configuration, reproduction steps, and the first compiler or linker error.

For larger changes, please open an Issue first to discuss the scope and approach.

### Third-Party Dependencies

[Dear ImGui](https://github.com/ocornut/imgui) is licensed under the MIT License.
See [ThirdParty/imgui/LICENSE.txt](ThirdParty/imgui/LICENSE.txt) for the original license.

The third-party dependency license does not determine the license of XToolbox itself.