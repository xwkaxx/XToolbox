/**
 * @file serial_window.h
 * @brief 自定义标题栏与 Win32 消息处理接口。
 * 绘制依赖 ImGui；消息处理独立于 ImGui，可在 CreateWindow 期间调用。
 */
#pragma once

#include <Windows.h>

// 返回当前窗口 DPI 下的标题栏高度，与窗口命中测试共用尺寸。
float GetSerialTitleBarHeight(HWND window);
// 在有效的 ImGui 帧内绘制自定义标题栏。
void DrawSerialTitleBar(HWND window);
// 在 ImGui 消息处理前调用；不依赖 ImGui 上下文，可在创建窗口期间调用。
// result 必须有效；返回 true 时 result 为该消息结果，返回 false 时由后续处理器接手。
bool SerialTool_HandleWindowMessage(HWND window, UINT message,
    WPARAM wParam, LPARAM lParam, LRESULT* result);
