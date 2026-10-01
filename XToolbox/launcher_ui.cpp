/**
 * @file launcher_ui.cpp
 * @brief 工具箱首页与串口工具进程启动。
 * 首页保留最近一次启动结果；子工具独立运行，启动器不持有其生命周期控制句柄。
 */
#include "launcher_ui.h"
#include "imgui.h"

#include <windows.h>
#include <filesystem>
#include <string>

// 返回 0 表示成功创建进程，否则返回 Windows 错误码。
/// @brief 使用 EXE 旁的绝对路径启动 SerialTool，成功返回 ERROR_SUCCESS。
static DWORD StartSerialTool()
{
    // 获取 XToolbox.exe 的完整路径。
    std::wstring executablePath(32768, L'\0');

    const DWORD length = GetModuleFileNameW(
        nullptr,
        executablePath.data(),
        static_cast<DWORD>(executablePath.size())
    );

    if (length == 0)
        return GetLastError();

    if (length >= executablePath.size())
        return ERROR_INSUFFICIENT_BUFFER;

    executablePath.resize(length);

    // SerialTool.exe 与 XToolbox.exe 放在同一个目录。
    // 路径基于当前 EXE，避免快捷方式或 IDE 改变工作目录后启动了错误位置的程序。
    const std::filesystem::path directory =
        std::filesystem::path(executablePath).parent_path();

    const std::filesystem::path toolPath =
        directory / L"SerialTool.exe";

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo{};

    // 显式指定应用程序路径并禁止句柄继承；不经命令解释器解析路径或参数。
    const BOOL started = CreateProcessW(
        toolPath.c_str(),     // 要启动的 EXE，使用完整路径
        nullptr,             // 暂时不传命令行参数
        nullptr,             // 使用默认进程安全属性
        nullptr,             // 使用默认线程安全属性
        FALSE,               // 不继承当前程序的句柄
        0,                   // 使用默认创建选项
        nullptr,             // 继承环境变量
        directory.c_str(),   // 新程序的工作目录
        &startupInfo,
        &processInfo
    );

    if (!started)
        return GetLastError();

    // 释放我们持有的句柄，不会关闭 SerialTool。
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    return ERROR_SUCCESS;
}

/// @brief 绘制入口和最近一次启动错误；每帧调用，不阻塞等待子进程结束。
void DrawLauncherUi()
{
    // static 让选择结果在多次函数调用之间保留。
    static const char* selectedTool = "None";
    static DWORD launchError = ERROR_SUCCESS;

    ImGui::SetNextWindowSize(
        ImVec2(420.0f, 280.0f),
        ImGuiCond_FirstUseEver
    );

    if (ImGui::Begin("XToolbox"))
    {
        ImGui::TextUnformatted("Select a tool");
        ImGui::TextUnformatted("中文字体测试 / English 123");
        ImGui::Separator();

        if (ImGui::Button("Serial Tool", ImVec2(220.0f, 40.0f)))
        {
            selectedTool = "Serial Tool";
            launchError = StartSerialTool();
        }

        // 以下两个入口目前只更新选中项，尚未关联可执行程序。
        if (ImGui::Button("Servo Tool", ImVec2(220.0f, 40.0f)))
        {
            selectedTool = "Servo Tool";
        }

        if (ImGui::Button("CMake Generator", ImVec2(220.0f, 40.0f)))
        {
            selectedTool = "CMake Generator";
        }

        ImGui::Separator();
        ImGui::Text("Selected: %s", selectedTool);

        if (launchError != ERROR_SUCCESS)
        {
            ImGui::Text(
                "SerialTool 启动失败，Windows 错误码：%lu",
                static_cast<unsigned long>(launchError)
            );

            ImGui::TextWrapped(
                "请确认 SerialTool.exe 已生成，且与 XToolbox.exe 位于同一目录。"
            );
        }
    }

    // 即使 Begin 因窗口折叠返回 false，仍必须调用 End 维持 ImGui 窗口栈。
    ImGui::End();
}