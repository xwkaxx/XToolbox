/**
 * @file launcher_ui.h
 * @brief 工具箱首页绘制入口。
 * 仅在主线程的有效 ImGui 帧内调用，串口工具通过独立进程启动。
 */
#pragma once

// 每帧调用一次，绘制工具选择界面。
void DrawLauncherUi();