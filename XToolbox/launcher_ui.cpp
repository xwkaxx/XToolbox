#include "launcher_ui.h"
#include "imgui.h"

#include <windows.h>
#include <filesystem>
#include <string>

// 返回 0 表示成功创建进程，否则返回 Windows 错误码。
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
    const std::filesystem::path directory =
        std::filesystem::path(executablePath).parent_path();

    const std::filesystem::path toolPath =
        directory / L"SerialTool.exe";

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo{};

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

    ImGui::End();
}