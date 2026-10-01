/**
 * @file serial_connection_panel.cpp
 * @brief 连接入口、参数面板和设备信息缓存。
 * 保存的端口名、暂存的连接参数与纯视觉动画状态分开管理；只在主线程访问。
 */
#include "serial_connection_panel.h"
#include "../application/serial_controller.h"
#include "serial_ui_widgets.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>

using namespace SerialUiWidgets;

namespace
{
    constexpr auto& BaudRates = SerialConnectionOptions::BaudRates;
    constexpr int CustomBaudIndex = SerialConnectionOptions::CustomBaudIndex;
}

/// @brief 重启限时连接引导；由后续绘制消费，不弹出阻塞对话框。
void SerialConnectionPanel::ShowConnectionHint()
{
    state_.connection_hint_started = ImGui::GetTime();
    state_.connection_hint_until = state_.connection_hint_started + 4.0;
}

// 仅更新界面缓存，设备枚举由 serial_devices 完成。
/// @brief 更新可选端口和查询错误，并使设备详情缓存失效。
void SerialConnectionPanel::RefreshPorts()
{
    state_.port_details_dirty = true;
    auto result = controller_.RefreshPorts();
    state_.ports = std::move(result.ports);
    state_.port_refresh_error = result.error;
    // 保留上次选择，即使设备暂时未插入，也不改选其他端口。
}

// 与其他参数一样占用表格一行，但端口列表在弹出时刷新。
/// @brief 在两列表格中绘制端口项，展开时按需查询设备列表。
void SerialConnectionPanel::DrawPortCombo()
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("端口号");
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0f);
    const std::string preview = controller_.Preferences().selected_port.empty() ? "请选择端口" : controller_.Preferences().selected_port;
    if (ImGui::BeginCombo("##Port", preview.c_str()))
    {
        // 仅在下拉窗口出现的第一帧查询，不在展开期间每帧刷新。
        if (ImGui::IsWindowAppearing())
            RefreshPorts();
        if (state_.port_refresh_error != ERROR_SUCCESS)
            ImGui::TextDisabled("读取端口失败，错误码：%lu", state_.port_refresh_error);
        else if (state_.ports.empty())
            ImGui::TextDisabled("未检测到串口");
        else
        {
            for (const std::string& port : state_.ports)
            {
                const bool selected = port == controller_.Preferences().selected_port;
                if (ImGui::Selectable(port.c_str(), selected))
                {
                    controller_.Preferences().selected_port = port;
                    controller_.SavePreferences();
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}
/// @brief 绘制不参与鼠标命中的前景引导；连接成功或计时结束后隐藏。
void SerialConnectionPanel::DrawConnectionHint(const ImVec2& center, float buttonSize)
{
    if (controller_.Status().connected)
    {
        state_.connection_hint_until = 0.0;
        return;
    }
    const double remaining = state_.connection_hint_until - ImGui::GetTime();
    if (remaining <= 0.0) return;
    const float scale = UiScale();
    const float elapsed = static_cast<float>(ImGui::GetTime() - state_.connection_hint_started);
    const float fadeIn = (std::clamp)(elapsed / 0.16f, 0.0f, 1.0f);
    const float fadeOut = (std::clamp)(static_cast<float>(remaining / 0.4), 0.0f, 1.0f);
    const float alpha = fadeIn * fadeOut;
    // 约一秒一个周期，平滑呼吸；不采用快速闪烁。
    const float pulse = 0.5f - 0.5f * std::cos(elapsed * 6.2831853f);
    const float drift = 4.0f * scale * pulse;
    const char* text = "请先打开串口";
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 topLeft(center.x + 30.0f * scale + drift,
        center.y + 54.0f * scale + drift + 6.0f * scale * (1.0f - fadeIn));
    const ImVec2 bottomRight(topLeft.x + textSize.x + 20.0f * scale,
        topLeft.y + textSize.y + 14.0f * scale);
    // 前景绘制不受左侧导航 child 裁剪，也不会挡住任何按钮的点击。
    ImDrawList* draw = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
    const ImU32 blue = ImGui::GetColorU32(ImVec4(0.02f, 0.42f, 0.77f, alpha));
    const float radius = buttonSize * 0.43f;
    // 外圈光晕随呼吸扩散，中心按钮仍保持清晰。
    for (int layer = 3; layer >= 1; --layer)
        draw->AddCircle(center, radius + (layer * 1.5f + pulse * 3.0f) * scale,
            ImGui::GetColorU32(ImVec4(0.02f, 0.55f, 0.90f,
                alpha * (0.08f + 0.07f * pulse))), 48, 3.0f * scale);
    draw->AddCircle(center, radius, blue, 48, (1.8f + 0.6f * pulse) * scale);
    const ImVec2 tip(center.x + radius * 0.76f + drift,
        center.y + radius * 0.76f + drift);
    const ImVec2 tail(topLeft.x + 12.0f * scale, topLeft.y - 5.0f * scale);
    draw->AddLine(tail, tip, blue, 2.5f * scale);
    const float dx = tail.x - tip.x, dy = tail.y - tip.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length > 0.0f)
    {
        const float ux = dx / length, uy = dy / length;
        const float head = 9.0f * scale;
        draw->AddLine(tip, ImVec2(tip.x + head * (ux - uy * 0.5f),
            tip.y + head * (uy + ux * 0.5f)), blue, 2.0f * scale);
        draw->AddLine(tip, ImVec2(tip.x + head * (ux + uy * 0.5f),
            tip.y + head * (uy - ux * 0.5f)), blue, 2.0f * scale);
    }
    draw->AddRectFilled(ImVec2(topLeft.x + 2 * scale, topLeft.y + 3 * scale),
        ImVec2(bottomRight.x + 2 * scale, bottomRight.y + 3 * scale),
        ImGui::GetColorU32(ImVec4(0, 0, 0, 0.10f * alpha)), 5.0f * scale);
    draw->AddRectFilled(topLeft, bottomRight,
        ImGui::GetColorU32(ImVec4(0.94f, 0.98f, 1.0f, alpha)), 5.0f * scale);
    draw->AddRect(topLeft, bottomRight,
        ImGui::GetColorU32(ImVec4(0.20f, 0.60f, 0.90f,
            alpha * (0.55f + 0.30f * pulse))), 5.0f * scale);
    draw->AddText(ImVec2(topLeft.x + 10.0f * scale, topLeft.y + 7.0f * scale), blue, text);
}

// 左侧工具栏的第一项：连接状态和打开/关闭入口。
/// @brief 绘制状态图标并将开关操作交给控制器。
void SerialConnectionPanel::DrawConnectionButton()
{
    const float scale = UiScale();
    const float buttonSize = 40.0f * scale;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    const bool clicked = ImGui::Button("##SerialConnection", ImVec2(buttonSize, buttonSize));
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    const bool hovered = ImGui::IsItemHovered();
    const float response = 1.0f - std::exp(-18.0f * ImGui::GetIO().DeltaTime);
    const float hoverTarget = hovered || ImGui::IsItemFocused() ? 1.0f : 0.0f;
    const float pressTarget = ImGui::IsItemActive() ? 1.0f : 0.0f;
    state_.connection_hover += (hoverTarget - state_.connection_hover) * response;
    state_.connection_press += (pressTarget - state_.connection_press) * response;
    const float imageSize = 36.0f * scale
        * (1.0f + 0.10f * state_.connection_hover - 0.10f * state_.connection_press);
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 center(minimum.x + buttonSize * 0.5f, minimum.y + buttonSize * 0.5f);
    DrawConnectionHint(center, buttonSize);
    ID3D11ShaderResourceView* icon = controller_.Status().connected
        ? icons_.connectedIcon.view.Get() : icons_.disconnectedIcon.view.Get();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (icon != nullptr)
    {
        const float half = imageSize * 0.5f;
        // 状态灯保留完整 UV，避免裁掉图片外围的圆环。
        drawList->AddImage(ImTextureRef(icon),
            ImVec2(center.x - half, center.y - half),
            ImVec2(center.x + half, center.y + half));
    }
    else
    {
        drawList->AddCircleFilled(center, imageSize * 0.3f,
            ImGui::GetColorU32(controller_.Status().connected
                ? ImVec4(0.13f, 0.71f, 0.45f, 1.0f)
                : ImVec4(0.55f, 0.59f, 0.62f, 1.0f)));
    }

    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(controller_.Status().connected ? "已连接" : "未连接");
        ImGui::TextUnformatted("数据接口：串口");
        ImGui::Text("端口：%s", controller_.Preferences().selected_port.empty() ? "未选择" : controller_.Preferences().selected_port.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted(controller_.Status().connected ? "点击断开连接" : "点击打开串口");
        ImGui::TextDisabled("Abc 显示按 UTF-8 解释；HEX 发送原始字节");
        ImGui::EndTooltip();
    }
    if (clicked)
    {
        if (controller_.Status().connected)
            controller_.Disconnect();
        else
            controller_.Connect();
    }
}
/// @brief 绘制配置区折叠入口，维护跨帧的图标过渡动画。
void SerialConnectionPanel::DrawNavigation()
{
    const float scale = UiScale();
    const float buttonSize = 40.0f * scale;

    // 固定点击区域，动画只改变区域内的图片，不影响布局。
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    const bool clicked = ImGui::Button("##ToggleSettings", ImVec2(buttonSize, buttonSize));
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    const bool hovered = ImGui::IsItemHovered();
    const bool focused = ImGui::IsItemFocused();
    const bool pressed = ImGui::IsItemActive();
    if (clicked)
        state_.settings_open = !state_.settings_open;
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    // 使用帧时间计算，避免不同刷新率下动画速度不一致。
    const float dt = ImGui::GetIO().DeltaTime;
    const float response = 1.0f - std::exp(-18.0f * dt);
    state_.arrow_hover += ((hovered || focused ? 1.0f : 0.0f)
        - state_.arrow_hover) * response;
    state_.arrow_press += ((pressed ? 1.0f : 0.0f)
        - state_.arrow_press) * response;

    // 约 0.18 秒完成一次翻转；快速重复点击可从当前位置反向播放。
    const float target = state_.settings_open ? 0.0f : 1.0f;
    const float step = dt / 0.18f;
    if (state_.arrow_transition < target)
    {
        state_.arrow_transition += step;
        if (state_.arrow_transition > target)
            state_.arrow_transition = target;
    }
    else if (state_.arrow_transition > target)
    {
        state_.arrow_transition -= step;
        if (state_.arrow_transition < target)
            state_.arrow_transition = target;
    }

    const float t = state_.arrow_transition;
    const float eased = t * t * (3.0f - 2.0f * t);
    // 水平方向先收窄再展开，在最窄处换图，避免两张箭头突然跳变。
    const float flipWidth = std::fabs(std::cos(eased * 3.14159265f));
    const bool illuminated = eased < 0.5f;
    ID3D11ShaderResourceView* icon = illuminated
        ? icons_.collapseIcon.view.Get() : icons_.expandIcon.view.Get();
    // 两张 PNG 都是 48×48，但荧光版箭头本体约高 19 像素，普通版约 26 像素。
    // 补偿透明留白，统一箭头本体大小，同时保留外围光晕和固定点击区域。
    const float artworkScale = illuminated ? 1.35f : 1.0f;
    const float imageSize = 32.0f * scale * artworkScale
        * (1.0f + 0.12f * state_.arrow_hover - 0.10f * state_.arrow_press);
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 center(minimum.x + buttonSize * 0.5f,
        minimum.y + buttonSize * 0.5f);
    const float halfWidth = imageSize * flipWidth * 0.5f;
    const float halfHeight = imageSize * 0.5f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (icon != nullptr)
    {
        // PNG 本身为深色，着色只作辅助；缩放和手形光标提供主要反馈。
        const ImVec4 tint(1.0f - 0.45f * state_.arrow_hover,
            1.0f - 0.15f * state_.arrow_hover, 1.0f, 1.0f);
        drawList->AddImage(ImTextureRef(icon),
            ImVec2(center.x - halfWidth, center.y - halfHeight),
            ImVec2(center.x + halfWidth, center.y + halfHeight),
            ImVec2(0.18f, 0.18f), ImVec2(0.82f, 0.82f),
            ImGui::GetColorU32(tint));
    }
    else
    {
        // 资源加载失败时仍提供可点击、可辨认的箭头。
        const float direction = state_.settings_open ? -1.0f : 1.0f;
        const float offset = 6.0f * scale;
        const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
        const ImVec2 tip(center.x + direction * offset, center.y);
        drawList->AddLine(ImVec2(center.x - direction * offset,
            center.y - offset), tip, color, 2.0f * scale);
        drawList->AddLine(tip, ImVec2(center.x - direction * offset,
            center.y + offset), color, 2.0f * scale);
    }

    if (hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", state_.settings_open
            ? "收起串口配置" : "展开串口配置");
}
/// @brief 仅在端口变更或显式刷新后重新查询设备描述。
void SerialConnectionPanel::DrawPortDetails()
{
    // 设备属性查询可能访问系统设备树，不应成为每帧开销；无信息的结果也参与缓存。
    if (state_.port_details_dirty || state_.port_details_name != controller_.Preferences().selected_port)
    {
        state_.port_details_dirty = false;
        state_.port_details_name = controller_.Preferences().selected_port;
        state_.port_details.clear();
        state_.port_details = controller_.PortDetails();
    }
    if (state_.port_details.empty()) return;
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.80f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(state_.port_details.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

/// @brief 绘制预置波特率及自定义输入；校验成功后才提交临时文本。
void SerialConnectionPanel::DrawBaudCombo()
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("波特率");
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0f);
    const DWORD value = controller_.ConnectionOptions().baud_index == CustomBaudIndex ? controller_.ConnectionOptions().custom_baud
        : BaudRates[(std::clamp)(controller_.ConnectionOptions().baud_index, 0, CustomBaudIndex - 1)];
    char preview[32];
    std::snprintf(preview, sizeof(preview), "%lu", static_cast<unsigned long>(value));
    bool openCustom = false;
    if (ImGui::BeginCombo("##Baud", preview))
    {
        for (int i = 0; i < CustomBaudIndex; ++i)
        {
            char label[32];
            std::snprintf(label, sizeof(label), "%lu", static_cast<unsigned long>(BaudRates[i]));
            const bool selected = controller_.ConnectionOptions().baud_index == i;
            if (ImGui::Selectable(label, selected)) controller_.ConnectionOptions().baud_index = i;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::Separator();
        if (ImGui::Selectable("自定义…", controller_.ConnectionOptions().baud_index == CustomBaudIndex))
        {
            std::snprintf(state_.custom_baud_input, sizeof(state_.custom_baud_input),
                "%lu", static_cast<unsigned long>(value));
            openCustom = true;
        }
        ImGui::EndCombo();
    }
    // 在 EndCombo 后打开弹窗，让弹窗 ID 归属稳定的父窗口，而不是临时下拉窗口。
    if (openCustom) ImGui::OpenPopup("CustomBaudInput");
    if (ImGui::BeginPopup("CustomBaudInput"))
    {
        ImGui::TextUnformatted("手动输入波特率");
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(220.0f * UiScale());
        const bool enter = ImGui::InputText("##CustomBaudValue", state_.custom_baud_input,
            sizeof(state_.custom_baud_input), ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
        // 用更宽的整数累积，每位都检查 DWORD 上限；不能依赖输入控件过滤来完成数值校验。
        std::uint64_t parsed = 0;
        bool valid = state_.custom_baud_input[0] != '\0';
        for (const char* p = state_.custom_baud_input; valid && *p; ++p)
        {
            if (*p < '0' || *p > '9') { valid = false; break; }
            parsed = parsed * 10 + static_cast<unsigned>(*p - '0');
            if (parsed > MAXDWORD) valid = false;
        }
        valid = valid && parsed > 0;
        if (!valid) ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.15f, 1), "请输入 1～4294967295 的整数");
        ImGui::TextDisabled("实际支持范围由串口设备和驱动决定");
        ImGui::BeginDisabled(!valid);
        const bool apply = ImGui::Button("确定");
        ImGui::EndDisabled();
        // 以确认作为提交边界，取消或非法输入不会污染当前连接参数。
        if (valid && (apply || enter))
        {
            controller_.ConnectionOptions().custom_baud = static_cast<DWORD>(parsed);
            controller_.ConnectionOptions().baud_index = CustomBaudIndex;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

/// @brief 汇总连接参数和收发统计，连接期间锁定会影响线路的参数。
void SerialConnectionPanel::DrawSettings()
{
    const float scale = UiScale();
    DrawSectionTitle("串口配置");
    ImGui::Separator();
    ImGui::Spacing();

    // 已打开时锁定通信参数，关闭后恢复编辑。
    ImGui::BeginDisabled(controller_.Status().connected);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 5.0f * scale));
    if (ImGui::BeginTable("SerialParameters", 2,
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed,
            96.0f * scale);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        DrawPortCombo();
        ImGui::EndTable();
    }
    DrawPortDetails();
    if (ImGui::BeginTable("SerialLineParameters", 2,
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 96.0f * scale);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        DrawBaudCombo();
        DrawCombo("校验位", "##Parity", &controller_.ConnectionOptions().parity_index,
            "None\0Odd\0Even\0");
        DrawCombo("数据位", "##DataBits", &controller_.ConnectionOptions().data_bits_index,
            "5\0" "6\0" "7\0" "8\0");
        DrawCombo("停止位", "##StopBits", &controller_.ConnectionOptions().stop_bits_index,
            "1\0" "1.5\0" "2\0");
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped(controller_.Status().connected ? "关闭串口后可修改参数" : "展开端口列表自动刷新");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("状态：%s", controller_.Status().connected ? "已连接" : "未连接");
    // 统计读取模型的累计 I/O 字节数，与当前可见文本长度和字体格式无关。
    ImGui::TextDisabled("接收：%llu B", static_cast<unsigned long long>(controller_.History().ReceivedBytes()));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("收发区最多保留 256 KiB 原始数据、2048 条记录；清空时重置收发计数。");
    ImGui::TextDisabled("发送：%llu B", static_cast<unsigned long long>(controller_.History().WrittenBytes()));
}
