/**
 * @file serial_ui_widgets.cpp
 * @brief 不含通信业务的 ImGui 视觉控件。
 * 依赖当前窗口的布局与 ID 栈；返回交互结果，由调用者决定业务动作。
 */
#include "serial_ui_widgets.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace SerialUiWidgets
{
    // 以 20 像素正文为设计基准，跟随当前窗口的字体和 DPI 缩放。
    /// @brief 获取相对 20 像素设计字号的比例，仅在有效 ImGui 帧内调用。
    float UiScale()
    {
        return ImGui::GetFontSize() / 20.0f;
    }

    /// @brief 对齐并绘制面板标题，不改变后续控件样式。
    void DrawSectionTitle(const char* text)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.04f, 0.39f, 0.69f, 1.0f), "%s", text);
    }

    // 只能在两列表格中调用：标签左对齐，右侧控件占满列宽。
    /// @brief 在当前两列表格追加一行参数；items 使用 ImGui 的双 NUL 结尾列表格式。
    void DrawCombo(
        const char* label,
        const char* id,
        int* selected,
        const char* items)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo(id, selected, items);
    }

    /// @brief 返回 HEX/Abc 图标预留宽度，保持父子布局的测量一致。
    float FormatButtonWidth()
    {
        return ImGui::GetFrameHeight() * 1.25f;
    }

    // 使用同一套圆角底板，保持图标尺寸与点击区域不变。
    /// @brief 在最近一个控件的矩形内绘制底板；调用前必须先提交对应按钮。
    void DrawIconButtonSurface(bool selected)
    {
        const ImVec2 low = ImGui::GetItemRectMin();
        const ImVec2 high = ImGui::GetItemRectMax();
        const bool hovered = ImGui::IsItemHovered();
        const bool pressed = ImGui::IsItemActive();
        const bool focused = ImGui::IsItemFocused();
        const float scale = UiScale();
        const float radius = 5.0f * scale;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (!pressed)
        {
            // 分层低透明度阴影，营造轻微凸起效果。
            for (int layer = 3; layer >= 1; --layer)
            {
                const float spread = layer * 0.7f * scale;
                draw->AddRectFilled(
                    ImVec2(low.x - spread, low.y - spread + 1.2f * scale),
                    ImVec2(high.x + spread, high.y + spread + 1.2f * scale),
                    ImGui::GetColorU32(ImVec4(0.16f, 0.23f, 0.28f, 0.025f)), radius + spread);
            }
        }
        const ImVec4 fill = pressed ? ImVec4(0.88f, 0.95f, 0.94f, 1.0f)
            : hovered ? ImVec4(0.96f, 0.99f, 0.99f, 1.0f) : ImVec4(1, 1, 1, 1);
        const ImVec4 border = (hovered || focused) ? ImVec4(0.50f, 0.78f, 0.76f, 1.0f)
            : selected ? ImVec4(0.81f, 0.89f, 0.88f, 1.0f) : ImVec4(0.88f, 0.90f, 0.92f, 1.0f);
        draw->AddRectFilled(low, high, ImGui::GetColorU32(fill), radius);
        draw->AddRect(low, high, ImGui::GetColorU32(border), radius, 0, scale);
    }

    // 点击区域与图片分开：PNG 保持透明，悬停缩放，Rx/Tx 根据开关选择不同图片。
    /// @brief 绘制图标按钮并返回点击；id 必须在当前 ID 栈内唯一。
    bool DrawIconButton(const SerialUiIcons& icons, const char* id, const ImageTexture& icon, bool active,
        const char* tooltip, float width, bool toggle)
    {
        const float height = ImGui::GetFrameHeight();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
        const bool clicked = ImGui::Button(id, ImVec2(width, height));
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        const bool hovered = ImGui::IsItemHovered();
        const bool pressed = ImGui::IsItemActive();
        const bool focused = ImGui::IsItemFocused();
        // 动画存入当前窗口的状态表，并复用按钮 ID；新增相同控件时必须用 PushID 隔离。
        const ImGuiID animationId = ImGui::GetID(id);
        ImGuiStorage* storage = ImGui::GetStateStorage();
        float emphasis = storage->GetFloat(animationId, 0.0f);
        const float target = pressed ? -0.08f : (hovered || focused) ? 0.08f : 0.0f;
        emphasis += (target - emphasis) * (1.0f - std::exp(-18.0f * ImGui::GetIO().DeltaTime));
        storage->SetFloat(animationId, emphasis);
        if (icon.view)
        {
            DrawIconButtonSurface(toggle && active);
            const ImVec2 minimum = ImGui::GetItemRectMin();
            const ImVec2 center(minimum.x + width * 0.5f, minimum.y + height * 0.5f);
            // 48×48 PNG 的透明留白不同，使用实际图案范围统一视觉大小。
            // UV 只影响显示，不改写资源图片。
            ImVec4 bounds(0, 0, 48, 48);
            float targetWidth = height * 0.68f;
            // 这里按资源对象地址识别裁剪区域；调用者须传入 icons 成员引用，不能传入复制品。
            if (&icon == &icons.displayHexIcon) { bounds = ImVec4(4, 17, 44, 33); targetWidth = height * 1.10f; }
            else if (&icon == &icons.displayTextIcon) { bounds = ImVec4(3, 16, 45, 33); targetWidth = height * 1.10f; }
            else if (&icon == &icons.showReceiveIcon) { bounds = ImVec4(3, 10, 45, 38); targetWidth = height * 0.78f; }
            else if (&icon == &icons.showSendIcon) { bounds = ImVec4(6, 13, 43, 38); targetWidth = height * 0.78f; }
            else if (&icon == &icons.showReceiveOffIcon) { bounds = ImVec4(3, 12, 45, 39); targetWidth = height * 0.78f; }
            else if (&icon == &icons.showSendOffIcon) { bounds = ImVec4(6, 13, 43, 37); targetWidth = height * 0.78f; }
            else if (&icon == &icons.fontResetIcon) bounds = ImVec4(11, 14, 37, 35);
            else if (&icon == &icons.fontIncreaseIcon) { bounds = ImVec4(9, 9, 39, 39); targetWidth = height * 0.55f; }
            else if (&icon == &icons.fontDecreaseIcon) { bounds = ImVec4(12, 22, 36, 26); targetWidth = height * 0.55f; }
            const float imageWidth = (std::min)(targetWidth, width * 0.90f) * (1.0f + emphasis);
            const float imageHeight = imageWidth * (bounds.w - bounds.y) / (bounds.z - bounds.x);
            // Rx/Tx 开关使用不同 PNG，不再叠加变淡效果。
            const float alpha = 1.0f;
            ImGui::GetWindowDrawList()->AddImage(ImTextureRef(icon.view.Get()),
                ImVec2(center.x - imageWidth * 0.5f, center.y - imageHeight * 0.5f),
                ImVec2(center.x + imageWidth * 0.5f, center.y + imageHeight * 0.5f),
                ImVec2(bounds.x / 48.0f, bounds.y / 48.0f),
                ImVec2(bounds.z / 48.0f, bounds.w / 48.0f),
                ImGui::GetColorU32(ImVec4(1, 1, 1, alpha)));
        }
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", tooltip);
        return clicked;
    }

    /// @brief 绘制无需纹理的橡皮按钮；清除哪类数据由调用者决定。
    bool DrawEraserButton(const char* id, const char* tooltip)
    {
        const float size = ImGui::GetFrameHeight();
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
        DrawIconButtonSurface(false);
        const ImVec2 low = ImGui::GetItemRectMin();
        const float unit = size / 32.0f;
        // 在固定 32 单位坐标系中定义图案，只在输出时缩放，避免各条线分别计算比例。
        const auto point = [&](float x, float y) { return ImVec2(low.x + x * unit, low.y + y * unit); };
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 ink = ImGui::GetColorU32(ImVec4(0.18f, 0.23f, 0.27f, 1));
        const ImVec2 body[] = { point(7, 19), point(19, 7), point(26, 14), point(14, 26) };
        draw->AddConvexPolyFilled(body, 4, ImGui::GetColorU32(ImVec4(1, 1, 1, 1)));
        const ImVec2 cap[] = { point(13, 13), point(19, 7), point(26, 14), point(20, 20) };
        draw->AddConvexPolyFilled(cap, 4, ink);
        draw->AddPolyline(body, 4, ink, ImDrawFlags_Closed, 1.3f * unit);
        draw->AddLine(point(13, 13), point(20, 20), ink, 1.3f * unit);
        draw->AddLine(point(14, 26), point(27, 26), ink, 1.3f * unit);
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", tooltip);
        return clicked;
    }
}
