/**
 * @file image_texture.h
 * @brief 图像纹理的 RAII 持有类型及资源加载接口。
 * 视图持有 GPU 资源引用；应在所属 DX11 设备的使用阶段内加载、绘制并释放。
 */
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

// 保存一张可供 ImGui 使用的图片纹理。
struct ImageTexture
{
    // 可为空；复制 ImageTexture 会增加 COM 引用计数，不会复制像素。
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    UINT width = 0;
    UINT height = 0;
};

// 从当前 EXE 的 RCDATA 资源中加载图片。
// 成功返回 S_OK；失败返回 HRESULT 错误码。
// 应在初始化阶段调用，不要每帧调用。
// device 为借用；有效设备调用先释放 image 旧结果，成功后才填充新视图和尺寸。
HRESULT LoadImageFromResource(
    ID3D11Device* device,
    int resourceId,
    ImageTexture& image
);