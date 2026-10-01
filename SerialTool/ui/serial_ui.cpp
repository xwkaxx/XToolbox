/**
 * @file serial_ui.cpp
 * @brief 串口界面的组合入口与共享资源所有者。
 * 仅在 ImGui 主线程调用；面板借用控制器和图标，通信生命周期由控制器统一管理。
 */
#include "serial_ui.h"
#include "../application/serial_controller.h"
#include "serial_window.h"
#include "serial_connection_panel.h"
#include "serial_send_panel.h"
#include "serial_output_panel.h"
#include "serial_ui_widgets.h"
#include "../resource.h"
#include "imgui.h"

#include <algorithm>

using namespace SerialUiWidgets;

namespace
{
    // 声明顺序保证被借用的控制器和资源早于面板构造，并晚于面板析构。
    SerialController g_controller;
    SerialUiIcons g_icons;
    SerialConnectionPanel g_connectionPanel{ g_controller, g_icons };
    SerialSendPanel g_sendPanel{ g_controller, g_icons };
    SerialOutputPanel g_outputPanel{ g_controller, g_icons };
    float g_sendHeight = 0.0f; // 布局偏好，保存 UiScale 缩放前的逻辑高度。

    /// @brief 按可用尺寸组合工具栏、输出区、分隔条和发送区。
    void DrawTerminal()
    {
        g_controller.InitializeEditor();
        const float scale = UiScale();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float width = ImGui::GetContentRegionAvail().x;

        // 清缓存按钮独立一行；下方按 Hex/Abc、时间戳、Rx、Tx、字号排列。
        g_outputPanel.DrawToolbar(width);

        g_controller.FlushMessages();

        const float sendButtonWidth = g_sendPanel.ButtonWidth();
        // 宽度计算与发送面板共用测量接口，避免父布局与子控件对换行阈值理解不同。
        const bool singleRow = width >= FormatButtonWidth() + sendButtonWidth + g_sendPanel.AppendWidth()
            + ImGui::GetFrameHeight() + style.ItemSpacing.x * 4.0f + 80.0f * scale;

        // 分隔条两侧保留最小高度，窗口缩小时限制发送区大小。
        const float splitterHeight = 6.0f * scale;
        const float minEditorHeight = singleRow ? ImGui::GetFrameHeight()
            : ImGui::GetFrameHeight() * 3.0f + style.ItemSpacing.y * 2.0f;
        const float fixedFooterHeight = splitterHeight + style.ItemSpacing.y * 3.0f;
        const float availableHeight = ImGui::GetContentRegionAvail().y;
        const float maxEditorHeight = (std::max)(minEditorHeight,
            availableHeight - fixedFooterHeight - ImGui::GetFrameHeight() * 2.0f);
        const float editorHeight = (std::clamp)(g_sendHeight * scale, minEditorHeight, maxEditorHeight);
        const float receiveHeight = (std::max)(1.0f,
            availableHeight - fixedFooterHeight - editorHeight);

        g_outputPanel.Draw(receiveHeight);
        const ImVec2 splitterPos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##SendAreaSplitter",
            ImVec2((std::max)(1.0f, ImGui::GetContentRegionAvail().x), splitterHeight));
        const bool splitterHovered = ImGui::IsItemHovered();
        const bool splitterActive = ImGui::IsItemActive();
        if (splitterHovered || splitterActive)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (splitterActive && ImGui::GetIO().MouseDelta.y != 0.0f)
            // 存储逻辑高度而不是屏幕像素，字号或 DPI 改变后仍能保持相同比例。
            g_sendHeight = (std::clamp)(editorHeight - ImGui::GetIO().MouseDelta.y,
                minEditorHeight, maxEditorHeight) / scale;
        if (splitterHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            g_sendHeight = 0.0f;
        const ImU32 splitterColor = ImGui::GetColorU32(splitterActive ? ImGuiCol_SeparatorActive
            : splitterHovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(splitterPos.x, splitterPos.y + splitterHeight * 0.5f),
            ImVec2(splitterPos.x + ImGui::GetItemRectSize().x, splitterPos.y + splitterHeight * 0.5f),
            splitterColor, splitterHovered || splitterActive ? 2.0f : 1.0f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("上下拖动调整发送区高度；双击恢复默认");
        if (g_sendPanel.Draw(singleRow, editorHeight))
            // 发送区只报告未连接，由导航面板负责提示；不让发送面板依赖连接按钮的实现。
            g_connectionPanel.ShowConnectionHint();
    }
}

/// @brief 加载偏好和全部图标；任一资源失败时回滚已取得资源。
HRESULT SerialTool_Init(ID3D11Device* device)
{
    SerialTool_Shutdown();
    g_controller.InitializePreferences();

    // 统一加载界面所需的嵌入资源；任何一张失败都释放本轮已加载的纹理。
    const struct { int id; ImageTexture* texture; } resources[] =
    {
        { IDR_PNG_COLLAPSE, &g_icons.collapseIcon },
        { IDR_PNG_EXPAND, &g_icons.expandIcon },
        { IDR_PNG_SERIAL_CONNECTED, &g_icons.connectedIcon },
        { IDR_PNG_SERIAL_DISCONNECTED, &g_icons.disconnectedIcon },
        { IDR_PNG_CLEAR, &g_icons.clearIcon },
        { IDR_PNG_TIMESTAMP_OFF, &g_icons.timestampOffIcon },
        { IDR_PNG_TIMESTAMP_ON, &g_icons.timestampOnIcon },
        { IDR_PNG_DISPLAY_HEX, &g_icons.displayHexIcon },
        { IDR_PNG_DISPLAY_TEXT, &g_icons.displayTextIcon },
        { IDR_PNG_SHOW_RECEIVE, &g_icons.showReceiveIcon },
        { IDR_PNG_SHOW_SEND, &g_icons.showSendIcon },
        { IDR_PNG_SHOW_RECEIVE_OFF, &g_icons.showReceiveOffIcon },
        { IDR_PNG_SHOW_SEND_OFF, &g_icons.showSendOffIcon },
        { IDR_PNG_FONT_RESET, &g_icons.fontResetIcon },
        { IDR_PNG_FONT_INCREASE, &g_icons.fontIncreaseIcon },
        { IDR_PNG_FONT_DECREASE, &g_icons.fontDecreaseIcon }
    };
    for (const auto& resource : resources)
    {
        const HRESULT hr = LoadImageFromResource(device, resource.id, *resource.texture);
        if (FAILED(hr))
        {
            SerialTool_Shutdown();
            return hr;
        }
    }
    return S_OK;
}

/// @brief 先结束通信，再释放图标；允许初始化失败和正常退出共用清理路径。
void SerialTool_Shutdown()
{
    g_controller.Shutdown();
    ImageTexture* textures[] =
    {
        &g_icons.collapseIcon, &g_icons.expandIcon, &g_icons.connectedIcon, &g_icons.disconnectedIcon, &g_icons.clearIcon,
        &g_icons.timestampOffIcon, &g_icons.timestampOnIcon,
        &g_icons.displayHexIcon, &g_icons.displayTextIcon, &g_icons.showReceiveIcon, &g_icons.showSendIcon,
        &g_icons.showReceiveOffIcon, &g_icons.showSendOffIcon, &g_icons.fontResetIcon, &g_icons.fontIncreaseIcon, &g_icons.fontDecreaseIcon
    };
    for (ImageTexture* texture : textures)
    {
        texture->view.Reset();
        texture->width = 0;
        texture->height = 0;
    }
}
/// @brief 消费后台结果并绘制一帧；调用前需存在有效 ImGui 上下文和已初始化的资源。
void SerialTool_Draw()
{
    // 在绘制前归并工作线程结果，使各面板读取的是本帧已更新的主线程状态。
    g_controller.Pump();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    // 颜色只作用于串口助手，离开本函数后恢复，避免污染其他窗口。
    const struct { ImGuiCol slot; ImVec4 color; } colors[] =
    {
        { ImGuiCol_WindowBg,      ImVec4(0.95f, 0.96f, 0.97f, 1.0f) },
        { ImGuiCol_ChildBg,       ImVec4(1.00f, 1.00f, 1.00f, 1.0f) },
        { ImGuiCol_Text,          ImVec4(0.14f, 0.18f, 0.22f, 1.0f) },
        { ImGuiCol_TextDisabled,  ImVec4(0.44f, 0.48f, 0.52f, 1.0f) },
        { ImGuiCol_Border,        ImVec4(0.80f, 0.83f, 0.86f, 1.0f) },
        { ImGuiCol_Separator,     ImVec4(0.85f, 0.87f, 0.89f, 1.0f) },
        { ImGuiCol_FrameBg,       ImVec4(0.98f, 0.99f, 1.00f, 1.0f) },
        { ImGuiCol_FrameBgHovered,ImVec4(0.93f, 0.96f, 0.99f, 1.0f) },
        { ImGuiCol_FrameBgActive, ImVec4(0.88f, 0.94f, 0.99f, 1.0f) },
        { ImGuiCol_Button,        ImVec4(0.94f, 0.96f, 0.98f, 1.0f) },
        { ImGuiCol_ButtonHovered, ImVec4(0.87f, 0.92f, 0.97f, 1.0f) },
        { ImGuiCol_ButtonActive,  ImVec4(0.80f, 0.88f, 0.95f, 1.0f) },
        { ImGuiCol_CheckMark,     ImVec4(0.06f, 0.46f, 0.76f, 1.0f) }
    };
    for (const auto& entry : colors)
        ImGui::PushStyleColor(entry.slot, entry.color);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    if (ImGui::Begin("SerialToolMain", nullptr, flags))
    {
        const float scale = UiScale();
        const float gap = 1.0f * scale;
        const float navigationWidth = 52.0f * scale;
        HWND window = static_cast<HWND>(viewport->PlatformHandleRaw);
        if (window == nullptr)
            window = static_cast<HWND>(viewport->PlatformHandle);
        if (window != nullptr)
        {
            const float titleHeight = GetSerialTitleBarHeight(window);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            if (ImGui::BeginChild("CustomTitleBar", ImVec2(0, titleHeight), 0,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
                DrawSerialTitleBar(window);
            ImGui::EndChild();
            ImGui::PopStyleVar();
            // 明确正文起点，避免 EndChild 自动附加的 ItemSpacing 产生多余空隙。
            ImGui::SetCursorPos(ImVec2(0, titleHeight));
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
            ImVec2(gap, ImGui::GetStyle().ItemSpacing.y));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
            ImVec2(6.0f * scale, 10.0f * scale));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.92f, 0.94f, 0.96f, 1));
        if (ImGui::BeginChild("Navigation", ImVec2(navigationWidth, 0),
            ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar))
        {
            // 工具栏独立使用紧凑间距，避免正文间距与额外留白叠加。
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 4.0f * scale));
            g_connectionPanel.DrawConnectionButton();
            g_connectionPanel.DrawNavigation();
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::SameLine();

        // 正常宽度时配置区在左侧；窄窗口改为上下排列，避免挤没终端。
        const float remainingWidth = ImGui::GetContentRegionAvail().x;
        const bool stacked = remainingWidth < 600.0f * scale;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
            ImVec2(14.0f * scale, 12.0f * scale));

        if (ImGui::BeginChild("Content", ImVec2(0, 0)))
        {
            if (g_connectionPanel.SettingsOpen())
            {
                float settingsHeight = 0.0f;
                if (stacked)
                    settingsHeight = ImGui::GetContentRegionAvail().y * 0.48f;

                if (ImGui::BeginChild("Settings",
                    ImVec2(stacked ? 0.0f : 280.0f * scale, settingsHeight),
                    ImGuiChildFlags_AlwaysUseWindowPadding))
                {
                    // 面板内部恢复正常控件间距，不沿用面板之间的 1 像素间隔。
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(12.0f * scale, 10.0f * scale));
                    g_connectionPanel.DrawSettings();
                    ImGui::PopStyleVar();
                }
                ImGui::EndChild();

                // 在两块面板之间绘制分界线，竖线使用渐变形成柔和边缘。
                // 只在配置展开时显示；上下布局时改画横线。
                const ImVec2 panelMin = ImGui::GetItemRectMin();
                const ImVec2 panelMax = ImGui::GetItemRectMax();
                const ImU32 dividerColor = ImGui::GetColorU32(ImVec4(0.76f, 0.80f, 0.85f, 1.0f));
                const float inset = 12.0f * scale;
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                if (stacked)
                {
                    const float y = panelMax.y + ImGui::GetStyle().ItemSpacing.y * 0.5f;
                    drawList->AddLine(ImVec2(panelMin.x + inset, y),
                        ImVec2(panelMax.x - inset, y), dividerColor, scale);
                }
                else
                {
                    // 分界带总宽 7 像素，中间 1.5 像素，两翼渐变至透明。
                    // 为它预留同宽间隙，避免渐变被相邻子窗口的白底盖住。
                    const float dividerWidth = 7.0f * scale;
                    const float centerWidth = 1.5f * scale;
                    const float x = panelMax.x + dividerWidth * 0.5f;
                    const float top = panelMin.y + inset;
                    const float bottom = panelMax.y - inset;
                    const ImU32 centerColor = ImGui::GetColorU32(ImVec4(0.72f, 0.77f, 0.83f, 0.35f));
                    const ImU32 edgeColor = ImGui::GetColorU32(ImVec4(0.72f, 0.77f, 0.83f, 0.0f));
                    drawList->AddRectFilledMultiColor(
                        ImVec2(panelMax.x, top), ImVec2(x - centerWidth * 0.5f, bottom),
                        edgeColor, centerColor, centerColor, edgeColor);
                    drawList->AddRectFilled(
                        ImVec2(x - centerWidth * 0.5f, top),
                        ImVec2(x + centerWidth * 0.5f, bottom), centerColor);
                    drawList->AddRectFilledMultiColor(
                        ImVec2(x + centerWidth * 0.5f, top), ImVec2(panelMax.x + dividerWidth, bottom),
                        centerColor, edgeColor, edgeColor, centerColor);
                    ImGui::SameLine(0.0f, dividerWidth);
                }
            }

            if (ImGui::BeginChild("Terminal", ImVec2(0, 0),
                ImGuiChildFlags_AlwaysUseWindowPadding))
            {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                    ImVec2(12.0f * scale, 10.0f * scale));
                DrawTerminal();
                ImGui::PopStyleVar();
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleVar();
    }
    // Begin/End 必须成对，即使 Begin 返回 false；样式栈也在所有分支汇合后统一恢复。
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(IM_ARRAYSIZE(colors));
}
