/**
 * @file serial_ui_widgets.h
 * @brief 共享图标集合与无业务副作用的绘制助手。
 * 依赖当前 ImGui 窗口、字体和 ID 栈；状态切换由调用方根据返回值处理。
 */
#pragma once

#include "image_texture.h"

// 由主界面拥有并统一加载/释放；面板只读借用，不能修改纹理生命周期。
struct SerialUiIcons
{
    ImageTexture collapseIcon;
    ImageTexture expandIcon;
    ImageTexture connectedIcon;
    ImageTexture disconnectedIcon;
    ImageTexture clearIcon;
    ImageTexture timestampOffIcon;
    ImageTexture timestampOnIcon;
    ImageTexture displayHexIcon;
    ImageTexture displayTextIcon;
    ImageTexture showReceiveIcon;
    ImageTexture showSendIcon;
    ImageTexture showReceiveOffIcon;
    ImageTexture showSendOffIcon;
    ImageTexture fontResetIcon;
    ImageTexture fontIncreaseIcon;
    ImageTexture fontDecreaseIcon;
};

namespace SerialUiWidgets
{
    /// @brief 以当前字体大小相对 20 像素设计字号计算缩放。
    float UiScale();
    /// @brief HEX/Abc 图标的标准控件宽度。
    float FormatButtonWidth();
    /// @brief 绘制带统一颜色的分组标题。
    void DrawSectionTitle(const char* text);
    /// @brief 在有效两列表格中追加参数行；items 为双 NUL 结束的标签列表。
    void DrawCombo(const char* label, const char* id, int* selected, const char* items);
    /// @brief 为最近提交的按钮绘制底板；不能在两次调用之间插入其他控件。
    void DrawIconButtonSurface(bool selected);
    /// @brief 返回点击状态；id 在窗口内唯一，icon 需直接引用 icons 成员以启用对应 UV 裁剪。
    bool DrawIconButton(const SerialUiIcons& icons, const char* id, const ImageTexture& icon,
        bool active, const char* tooltip, float width, bool toggle = false);
    /// @brief 绘制矢量橡皮按钮并返回点击，不直接清除数据。
    bool DrawEraserButton(const char* id, const char* tooltip);
}
