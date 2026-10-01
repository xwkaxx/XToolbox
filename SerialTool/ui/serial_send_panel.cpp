/**
 * @file serial_send_panel.cpp
 * @brief 发送编辑区、后缀选择和连续发送进度。
 * 面板只处理交互；字节解析、提交和定时调度分别交由模型、控制器和会话。
 */
#include "serial_send_panel.h"
#include "../application/serial_controller.h"
#include "serial_ui_widgets.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

using namespace SerialUiWidgets;

/// @brief 按最长后缀名称测量宽度，供自身绘制和父布局共用。
float SerialSendPanel::AppendWidth() const
{
    return ImGui::CalcTextSize("CRC16/Modbus").x
        + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f;
}

/// @brief 返回当前发送按钮组的宽度，包含右侧选项箭头。
float SerialSendPanel::ButtonWidth() const
{
    return ImGui::CalcTextSize(controller_.Preferences().timed_send ? "连续发送(S)" : "发送").x
        + ImGui::GetStyle().FramePadding.x * 2.0f + 12.0f * UiScale() + ImGui::GetFrameHeight();
}

/// @brief 绘制发送区并提交本帧操作。
/// @return 仅未连接的发送请求返回 true，通知外层显示连接引导。
bool SerialSendPanel::Draw(bool singleRow, float editorHeight)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float buttonWidth = ButtonWidth();
    const ImVec2 areaStart = ImGui::GetCursorPos();
    const float areaWidth = ImGui::GetContentRegionAvail().x;
    const float frameHeight = ImGui::GetFrameHeight();
    const float toolbarHeight = singleRow ? frameHeight : frameHeight * 2.0f + style.ItemSpacing.y;
    // 单行工具栏与紧凑输入框是两个条件；用户拉高发送区后，输入框仍可独占上方空间。
    const bool compact = singleRow && editorHeight < frameHeight * 2.0f + style.ItemSpacing.y;
    const float inputHeight = compact ? editorHeight
        : (std::max)(frameHeight, editorHeight - toolbarHeight - style.ItemSpacing.y);
    const float inputWidth = (std::max)(1.0f, compact
        ? areaWidth - FormatButtonWidth() - AppendWidth() - buttonWidth - frameHeight - style.ItemSpacing.x * 4.0f
        : areaWidth);
    if (compact)
        ImGui::SetCursorPos(ImVec2(areaStart.x + FormatButtonWidth() + style.ItemSpacing.x, areaStart.y));
    bool enter = false;
    if (controller_.Editor().IsHex())
        enter = ImGui::InputTextMultiline("##SendBufferHex", controller_.Editor().Buffer(), controller_.Editor().Capacity(),
            ImVec2(inputWidth, inputHeight),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
    else
    {
        enter = ImGui::InputTextMultiline("##SendBufferText", controller_.Editor().Buffer(), controller_.Editor().Capacity(),
            ImVec2(inputWidth, inputHeight), ImGuiInputTextFlags_EnterReturnsTrue);
        // ImGui 直接编辑字符缓冲；同步 token 后才能保留未改动占位符背后的原始字节。
        controller_.Editor().SyncText();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(controller_.Editor().IsHex() ? "HEX 原始字节；Enter 发送，Ctrl+Enter 换行"
            : "UTF-8 字符；Enter 换行，Ctrl+Enter 发送；方框保留原始字节");

    ImGui::SetCursorPos(ImVec2(areaStart.x, areaStart.y + editorHeight - toolbarHeight));
    const float toolbarLeft = areaStart.x;
    const float toolbarWidth = areaWidth;
    if (DrawIconButton(icons_, "##SendMode", controller_.Editor().IsHex() ? icons_.displayHexIcon : icons_.displayTextIcon,
        false, controller_.Editor().IsHex() ? "发送模式：HEX；点击切换 UTF-8 字符串"
            : "发送模式：UTF-8 字符串；点击切换 HEX", FormatButtonWidth()))
    {
        controller_.ToggleSendMode();
    }

    if (singleRow)
    {
        ImGui::SameLine();
        const float rightGroupWidth = frameHeight + AppendWidth() + style.ItemSpacing.x * 2.0f + buttonWidth;
        ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(),
            toolbarLeft + toolbarWidth - rightGroupWidth));
    }
    if (DrawEraserButton("##EraseSendText", "清空发送输入框（不停止正在进行的连续发送）"))
    {
        controller_.ClearSendText();
        enter = false;
    }
    ImGui::SameLine();
    // 本区域使用同一份进度快照，避免在同一帧混用不同批次的完成次数和任务总数。
    const auto status = controller_.Status();
    const bool running = status.timed_active;
    const int total = status.timed_total;
    const std::uint64_t completed = status.timed_completed;
    if (running)
    {
        char counter[64];
        if (total < 0)
            std::snprintf(counter, sizeof(counter), "%llu/∞", static_cast<unsigned long long>(completed));
        else
            std::snprintf(counter, sizeof(counter), "%llu/%d", static_cast<unsigned long long>(completed), total);
        const float progressWidth = AppendWidth();
        const float height = ImGui::GetFrameHeight();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##TimedSendProgress", ImVec2(progressWidth, height));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 green = ImGui::GetColorU32(ImVec4(0.07f, 0.70f, 0.58f, 1));
        const float unit = UiScale();
        const float angle = static_cast<float>(std::fmod(ImGui::GetTime() * 4.0, 6.2831853));
        const ImVec2 center(pos.x + 10.0f * unit, pos.y + height * 0.43f);
        const ImVec2 a(center.x + std::cos(angle) * 6.0f * unit, center.y + std::sin(angle) * 6.0f * unit);
        const ImVec2 b(center.x - std::cos(angle) * 6.0f * unit, center.y - std::sin(angle) * 6.0f * unit);
        draw->AddLine(a, b, green, 1.5f * unit);
        draw->AddCircleFilled(a, 2.5f * unit, green);
        draw->AddCircleFilled(b, 2.5f * unit, green);
        const float textX = pos.x + 24.0f * unit;
        const float naturalWidth = ImGui::CalcTextSize(counter).x;
        const float textSize = ImGui::GetFontSize() * (std::min)(1.0f,
            (std::max)(1.0f, progressWidth - 26.0f * unit) / (std::max)(1.0f, naturalWidth));
        draw->AddText(ImGui::GetFont(), textSize,
            ImVec2(textX, pos.y + (height - textSize) * 0.4f),
            ImGui::GetColorU32(ImGuiCol_Text), counter);
        const float barY = pos.y + height - 3.0f * unit;
        draw->AddRectFilled(ImVec2(pos.x, barY), ImVec2(pos.x + progressWidth, barY + 2.0f * unit),
            ImGui::GetColorU32(ImVec4(0.88f, 0.94f, 0.93f, 1)));
        // 有限任务按真实成功次数显示比例；无限任务只显示活动标识，不能伪造完成百分比。
        if (total > 0)
        {
            const float fraction = (std::min)(1.0f, static_cast<float>(completed) / total);
            draw->AddRectFilled(ImVec2(pos.x, barY),
                ImVec2(pos.x + progressWidth * fraction, barY + 2.0f * unit), green);
        }
        else
        {
            const float travel = static_cast<float>(std::fmod(ImGui::GetTime(), 1.5) / 1.5);
            const float x = pos.x + travel * progressWidth * 0.75f;
            draw->AddRectFilled(ImVec2(x, barY),
                ImVec2(x + progressWidth * 0.25f, barY + 2.0f * unit), green);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("本轮实际成功发送：%s", counter);
    }
    else
    {
        ImGui::SetNextItemWidth(AppendWidth());
        const char* appendOptions[] = { "无追加", "\\n", "\\r", "\\n\\r", "\\r\\n", "CRC16/Modbus" };
        if (ImGui::Combo("##SendAppend", &controller_.Preferences().send_append, appendOptions, IM_ARRAYSIZE(appendOptions)))
            controller_.SavePreferences();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(controller_.Preferences().send_append == 5
                ? "自动计算并追加 Modbus RTU CRC16（低字节在前）；输入内容不要包含 CRC"
                : "发送时追加所选字节，不改变输入框内容");
    }
    ImGui::SameLine();
    const float arrowWidth = ImGui::GetFrameHeight();
    if (running || controller_.Preferences().timed_send)
        ImGui::PushStyleColor(ImGuiCol_Text, running ? ImVec4(0.90f, 0.15f, 0.15f, 1)
            : ImVec4(0.04f, 0.39f, 0.75f, 1));
    const bool clicked = ImGui::Button(running ? "终止##Send" : controller_.Preferences().timed_send ? "连续发送(S)##Send" : "发送##Send",
        ImVec2(buttonWidth - arrowWidth, 0.0f));
    if (running || controller_.Preferences().timed_send) ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::ArrowButton("##SendOptions", ImGuiDir_Down))
        ImGui::OpenPopup("SendOptionsPopup");
    if (ImGui::BeginPopup("SendOptionsPopup"))
    {
        // 本轮任务已经捕获启动参数；运行时禁用设置，避免界面暗示修改能影响当前任务。
        ImGui::BeginDisabled(running);
        const bool previousTimed = controller_.Preferences().timed_send;
        const int previousInterval = controller_.Preferences().send_interval_ms;
        const int previousRepeats = controller_.Preferences().send_repeat_count;
        ImGui::Checkbox("连续发送", &controller_.Preferences().timed_send);
        ImGui::SetNextItemWidth(160.0f * UiScale());
        if (ImGui::InputInt("发送后延时(ms)", &controller_.Preferences().send_interval_ms, 1, 100))
            controller_.Preferences().send_interval_ms = (std::clamp)(controller_.Preferences().send_interval_ms, 1, 86400000);
        ImGui::SetNextItemWidth(160.0f * UiScale());
        const int previousCount = controller_.Preferences().send_repeat_count;
        if (ImGui::InputInt("发送总次数", &controller_.Preferences().send_repeat_count, 1, 10))
        {
            // 加减按钮跨过 0：1 减一次到 -1，-1 加一次到 1。
            if (controller_.Preferences().send_repeat_count == 0)
                controller_.Preferences().send_repeat_count = previousCount == -1 ? 1 : -1;
            else if (controller_.Preferences().send_repeat_count < -1)
                controller_.Preferences().send_repeat_count = previousCount;
            else if (controller_.Preferences().send_repeat_count > 1000000)
                controller_.Preferences().send_repeat_count = 1000000;
            controller_.Preferences().timed_send = true;
        }
        if (previousTimed != controller_.Preferences().timed_send || previousInterval != controller_.Preferences().send_interval_ms
            || previousRepeats != controller_.Preferences().send_repeat_count)
            controller_.SavePreferences();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("-1：无限发送；正整数：发送总次数；不接受 0 或其他负数");
        if (controller_.Preferences().send_repeat_count == -1)
            ImGui::TextDisabled("无限发送，直到点击终止");
        else
            ImGui::TextDisabled("本轮共发送 %d 次（包含第一帧）", controller_.Preferences().send_repeat_count);
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::TextDisabled(running ? "正在发送；点击终止结束本轮" : "点击发送启动；未开启连续发送则只发送一次");
        ImGui::TextDisabled("本轮使用启动时的数据、模式和追加设置");
        ImGui::EndPopup();
    }
    // 显式禁止按键重复，防止长按回车在多帧内重复启动或停止任务。
    const bool enterOnce = enter && (ImGui::IsKeyPressed(ImGuiKey_Enter, false)
        || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    if (!clicked && !enterOnce) return false;
    if (running)
    {
        controller_.StopTimedSend();
        return false;
    }
    return controller_.Send() == SerialSendResult::NotConnected;
}
