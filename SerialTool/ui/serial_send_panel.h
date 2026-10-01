/**
 * @file serial_send_panel.h
 * @brief 发送区的测量与绘制接口。
 * 借用控制器和图标；调用前由主界面初始化编辑器，绘制尺寸与字体在同一帧保持一致。
 */
#pragma once

class SerialController;
struct SerialUiIcons;

// 发送编辑框、选项及进度绘制。内容由控制器拥有，尺寸由主界面分配。
class SerialSendPanel
{
public:
    SerialSendPanel(SerialController& controller, const SerialUiIcons& icons)
        : controller_(controller), icons_(icons) {}
    /// @brief 测量后缀选择/进度位置所需宽度。
    float AppendWidth() const;
    /// @brief 测量当前模式下发送按钮及箭头的总宽度。
    float ButtonWidth() const;
    // 返回 true 表示用户请求发送但未连接，由主界面协调连接提示。
    // singleRow 描述工具栏排列，editorHeight 为整个发送区域的像素高度。
    bool Draw(bool singleRow, float editorHeight);

private:
    SerialController& controller_;
    const SerialUiIcons& icons_;
};
