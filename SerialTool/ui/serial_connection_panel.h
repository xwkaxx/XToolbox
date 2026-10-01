/**
 * @file serial_connection_panel.h
 * @brief 连接导航与配置区的跨帧状态。
 * 控制器和图标均为借用，必须比面板活得更久；所有 Draw 方法仅在有效 ImGui 帧内调用。
 */
#pragma once

#include <Windows.h>
#include <string>
#include <vector>

class SerialController;
struct SerialUiIcons;
struct ImVec2;

// 连接入口、导航动画和串口配置控件；仅拥有本面板的显示状态。
class SerialConnectionPanel
{
public:
    SerialConnectionPanel(SerialController& controller, const SerialUiIcons& icons)
        : controller_(controller), icons_(icons) {}
    /// @brief 绘制连接状态并处理打开/关闭请求。
    void DrawConnectionButton();
    /// @brief 绘制折叠按钮，更新 SettingsOpen 对应的布局状态。
    void DrawNavigation();
    /// @brief 绘制参数、设备摘要和统计；外层负责分配区域。
    void DrawSettings();
    bool SettingsOpen() const { return state_.settings_open; }
    /// @brief 启动限时引导，后续 DrawConnectionButton 绘制提示。
    void ShowConnectionHint();

private:
    void RefreshPorts();
    void DrawPortCombo();
    void DrawConnectionHint(const ImVec2& center, float buttonSize);
    void DrawPortDetails();
    void DrawBaudCombo();
    struct State
    {
        bool settings_open = true;
        float connection_hover = 0.0f;
        float connection_press = 0.0f;
        double connection_hint_started = 0.0;
        double connection_hint_until = 0.0; // 未连接时发送，短暂提示连接入口。
        // 动画状态跨帧保留：0 为收起箭头，1 为展开箭头。
        float arrow_transition = 0.0f;
        float arrow_hover = 0.0f;
        float arrow_press = 0.0f;

        // 设备查询缓存与动画状态分开；只有展开列表、端口变化或脏标记才触发查询。
        std::vector<std::string> ports;
        std::string port_details;
        std::string port_details_name;
        bool port_details_dirty = true;
        DWORD port_refresh_error = ERROR_SUCCESS;
        // 弹窗编辑草稿，确认通过范围校验后才写回控制器。
        char custom_baud_input[16] = "115200";
    };

    State state_;
    SerialController& controller_;
    const SerialUiIcons& icons_;
};
