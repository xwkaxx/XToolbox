/**
 * @file serial_window.cpp
 * @brief 无系统标题栏窗口的绘制与原生命中处理。
 * 标题按钮布局和 Win32 命中区域共享逻辑尺寸；窗口消息路径不能依赖 ImGui 初始化。
 */
#include "serial_window.h"
#include "imgui.h"

namespace
{
    // 标题栏绘制与 Win32 命中测试共用这些尺寸，单位为 96 DPI 下的像素。
    constexpr float TitleHeight = 40.0f;
    constexpr float TitleButtonWidth = 44.0f;

    /// @brief 用窗口当前 DPI 将逻辑尺寸换算为原生客户区像素。
    float WindowScale(HWND window)
    {
        return static_cast<float>(::GetDpiForWindow(window)) / 96.0f;
    }
}

/// @brief 返回当前窗口 DPI 下的标题高度，供主界面预留空间。
float GetSerialTitleBarHeight(HWND window)
{
    return TitleHeight * WindowScale(window);
}

/// @brief 绘制四个窗口控制按钮，将系统行为交给 Win32 窗口消息。
void DrawSerialTitleBar(HWND window)
{
    const float scale = WindowScale(window);
    const float height = TitleHeight * scale;
    const float buttonWidth = TitleButtonWidth * scale;
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
        IM_COL32(255, 255, 255, 255));
    // 标题文字不得进入右侧四个按钮范围，此范围也必须与 WM_NCHITTEST 的排除区域一致。
    draw->PushClipRect(origin, ImVec2(origin.x + width - 4 * buttonWidth, origin.y + height), true);
    draw->AddText(ImVec2(origin.x + 16 * scale,
        origin.y + (height - ImGui::GetFontSize()) * 0.5f),
        IM_COL32(10, 99, 176, 255), "SerialTool · 串口助手");
    draw->PopClipRect();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.93f, 0.96f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.81f, 0.87f, 0.92f, 1));
    const bool pinned = (::GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    const bool maximized = ::IsZoomed(window) != FALSE;
    const char* ids[] = { "##PinWindow", "##MinWindow", "##MaxWindow", "##CloseWindow" };
    const char* tips[] = { pinned ? "取消置顶" : "窗口置顶", "最小化",
        maximized ? "还原" : "最大化", "关闭" };
    for (int i = 0; i < 4; ++i)
    {
        const ImVec2 p(origin.x + width - (4 - i) * buttonWidth, origin.y);
        ImGui::SetCursorScreenPos(p);
        if (i == 3)
        {
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.16f, 0.20f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.73f, 0.09f, 0.13f, 1));
        }
        const bool clicked = ImGui::Button(ids[i], ImVec2(buttonWidth, height));
        const bool hovered = ImGui::IsItemHovered();
        if (i == 3)
            ImGui::PopStyleColor(2);
        const ImU32 color = i == 3 && hovered ? IM_COL32_WHITE
            : (i == 0 && pinned ? IM_COL32(10, 110, 190, 255) : IM_COL32(48, 57, 66, 255));
        const ImVec2 c(p.x + buttonWidth * 0.5f, p.y + height * 0.5f);
        const float r = 5.0f * scale;
        const float line = 1.5f * scale;
        if (i == 0)
        {
            // 简化的图钉，置顶时显示蓝色。
            draw->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y - r), color, line);
            draw->AddLine(ImVec2(c.x - r * 0.6f, c.y - r), ImVec2(c.x - r, c.y + r * 0.4f), color, line);
            draw->AddLine(ImVec2(c.x + r * 0.6f, c.y - r), ImVec2(c.x + r, c.y + r * 0.4f), color, line);
            draw->AddLine(ImVec2(c.x - r, c.y + r * 0.4f), ImVec2(c.x + r, c.y + r * 0.4f), color, line);
            draw->AddLine(ImVec2(c.x, c.y + r * 0.4f), ImVec2(c.x, c.y + r * 1.5f), color, line);
        }
        else if (i == 1)
            draw->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), color, line);
        else if (i == 2)
        {
            if (maximized)
            {
                draw->AddRect(ImVec2(c.x - r + 3 * scale, c.y - r),
                    ImVec2(c.x + r, c.y + r - 3 * scale), color, 0, 0, line);
                draw->AddRectFilled(ImVec2(c.x - r, c.y - r + 3 * scale),
                    ImVec2(c.x + r - 3 * scale, c.y + r), IM_COL32(255, 255, 255, 255));
            }
            draw->AddRect(ImVec2(c.x - r, c.y - r + (maximized ? 3 * scale : 0)),
                ImVec2(c.x + r - (maximized ? 3 * scale : 0), c.y + r), color, 0, 0, line);
        }
        else
        {
            draw->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), color, line);
            draw->AddLine(ImVec2(c.x + r, c.y - r), ImVec2(c.x - r, c.y + r), color, line);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", tips[i]);
        if (clicked)
        {
            if (i == 0)
                ::SetWindowPos(window, pinned ? HWND_NOTOPMOST : HWND_TOPMOST,
                    0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            else
                // 投递系统命令，让最大化、还原和关闭走正常消息循环，避免绘制途中重入窗口销毁。
                ::PostMessageW(window, WM_SYSCOMMAND,
                    i == 1 ? SC_MINIMIZE : i == 2 ? (maximized ? SC_RESTORE : SC_MAXIMIZE) : SC_CLOSE, 0);
        }
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
    draw->AddLine(ImVec2(origin.x, origin.y + height - 1),
        ImVec2(origin.x + width, origin.y + height - 1), IM_COL32(225, 230, 235, 255));
}
// 由 main.cpp 的 WndProc 在 ImGui 消息处理之前调用。
// 不依赖 ImGui 上下文，因此创建窗口期间也能安全处理消息。
/// @brief 处理自绘边框相关消息；返回 true 时调用方直接返回 result。
/// @param result 有效输出指针；返回 false 时不能把其内容当成已处理结果。
bool SerialTool_HandleWindowMessage(HWND window, UINT message,
    WPARAM wParam, LPARAM lParam, LRESULT* result)
{
    const float scale = WindowScale(window);
    if (message == WM_NCCALCSIZE && wParam != FALSE)
    {
        // 将系统标题栏交给客户区绘制；最大化时限定在工作区，避免遮住任务栏。
        if (::IsZoomed(window))
        {
            MONITORINFO monitor = { sizeof(MONITORINFO) };
            if (::GetMonitorInfoW(::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
                reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0] = monitor.rcWork;
        }
        *result = 0;
        return true;
    }
    if (message == WM_GETMINMAXINFO)
    {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize.x = static_cast<LONG>(640 * scale);
        limits->ptMinTrackSize.y = static_cast<LONG>(420 * scale);
        *result = 0;
        return true;
    }
    if (message == WM_NCHITTEST)
    {
        // 有符号转换支持位于主显示器左侧/上方的屏幕坐标。
        POINT point = { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
        ::ScreenToClient(window, &point);
        RECT client = {};
        ::GetClientRect(window, &client);
        const int border = static_cast<int>(6 * scale);
        // 最大化时取消边框缩放命中；普通窗口先判断边角，随后才判断标题拖动区域。
        if (!::IsZoomed(window))
        {
            const bool left = point.x < border;
            const bool right = point.x >= client.right - border;
            const bool top = point.y < border;
            const bool bottom = point.y >= client.bottom - border;
            *result = top && left ? HTTOPLEFT : top && right ? HTTOPRIGHT
                : bottom && left ? HTBOTTOMLEFT : bottom && right ? HTBOTTOMRIGHT
                : left ? HTLEFT : right ? HTRIGHT : top ? HTTOP : bottom ? HTBOTTOM : HTCLIENT;
            if (*result != HTCLIENT)
                return true;
        }
        // 右侧四个按钮保留客户区鼠标事件；其余标题区域由 Windows 处理拖动和双击。
        *result = point.y >= 0 && point.y < TitleHeight * scale
            && point.x < client.right - 4 * TitleButtonWidth * scale ? HTCAPTION : HTCLIENT;
        return true;
    }
    return false;
}
