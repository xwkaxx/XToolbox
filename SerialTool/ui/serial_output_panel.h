/**
 * @file serial_output_panel.h
 * @brief 输出绘制与选择状态；数据所有权仍属于控制器。
 * 面板、控制器和图标在主线程共用，外部借用对象须覆盖面板整个生存期。
 */
#pragma once

#include "imgui.h"
#include <cstddef>
#include <string>
#include <vector>

class SerialController;
struct SerialUiIcons;

// 输出工具栏、折行、文字选择与复制。历史数据仍由控制器拥有。
class SerialOutputPanel
{
public:
    SerialOutputPanel(SerialController& controller, const SerialUiIcons& icons)
        : controller_(controller), icons_(icons) {}
    /// @brief 在当前区域绘制工具栏，width 为可用屏幕宽度。
    void DrawToolbar(float width);
    /// @brief 创建给定高度的输出 child，并维护选择、折行和滚动。
    void Draw(float height);

private:
    // 一个视觉行片段；begin/end 是逻辑 UTF-8 文本的半开字节区间，不是字符数量。
    struct OutputTextRun
    {
        ImVec2 position;
        ImU32 color;
        size_t begin;
        size_t end;
    };

    void DrawClearButton();
    void DrawTimestampButton();
    void DrawEraseTextButton();
    void DrawOutputToolbar(float width);
    void DrawSelectableOutput(const std::string& text, const std::vector<OutputTextRun>& runs,
        float lineHeight, bool& selecting);

    SerialController& controller_;
    const SerialUiIcons& icons_;
    float clear_hover_ = 0.0f;
    float clear_press_ = 0.0f;
    float timestamp_emphasis_ = 0.0f;
    float previous_width_ = 0.0f;
    // 用上帧逻辑文本识别纯追加；发生裁剪或重排内容变化时废弃旧选区。
    std::string previous_text_;
    // 锚点和光标允许反向选择，均保存 UTF-8 字节边界，由绘制流程限定到文本长度。
    std::size_t anchor_ = 0;
    std::size_t caret_ = 0;
    bool dragging_ = false;
};
