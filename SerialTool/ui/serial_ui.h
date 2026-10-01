/**
 * @file serial_ui.h
 * @brief 串口界面对程序入口暴露的生命周期。
 * 调用顺序：建立 DX11/ImGui 后 Init，每帧 Draw，销毁后端及设备前 Shutdown；仅限主线程。
 */
#pragma once

#include <d3d11.h>

// 恢复配置并加载各面板借用的图片纹理，只在启动时调用。
HRESULT SerialTool_Init(ID3D11Device* device);

// 每帧处理通信结果并组合连接、输出、发送面板。
void SerialTool_Draw();

// 停止通信会话并释放图片纹理，在销毁 DX11 设备之前调用。
void SerialTool_Shutdown();
