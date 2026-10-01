/**
 * @file image_texture.cpp
 * @brief 将 EXE 内嵌图像经 WIC 解码为 DX11 只读纹理视图。
 * 局部 COM 对象按作用域释放，返回的 ImageTexture 持有最终 GPU 资源引用。
 */
#include "image_texture.h"

#include <wincodec.h>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    // 与成功的 CoInitializeEx 配对。
    struct ComScope
    {
        ~ComScope()
        {
            CoUninitialize();
        }
    };
}

/// @brief 读取 RT_RCDATA 图像资源；成功返回尺寸和可供 ImGui 使用的视图。
/// @param device 借用的有效 DX11 设备；空指针直接返回 E_INVALIDARG 并保留旧 image。
/// @param image 非空设备调用先清空旧结果，后续失败时保持空结果。
HRESULT LoadImageFromResource(
    ID3D11Device* device,
    int resourceId,
    ImageTexture& image)
{
    if (device == nullptr)
        return E_INVALIDARG;

    // 先清理上一次加载的结果。
    image.view.Reset();
    image.width = 0;
    image.height = 0;

    // 1. 从当前 EXE 中查找资源。
    HMODULE module = GetModuleHandleW(nullptr);

    HRSRC resource = FindResourceW(
        module,
        MAKEINTRESOURCEW(resourceId),
        RT_RCDATA
    );

    if (resource == nullptr)
        return HRESULT_FROM_WIN32(GetLastError());

    const DWORD resourceSize = SizeofResource(module, resource);
    HGLOBAL loadedResource = LoadResource(module, resource);

    if (loadedResource == nullptr)
        return HRESULT_FROM_WIN32(GetLastError());

    BYTE* resourceData =
        static_cast<BYTE*>(LockResource(loadedResource));

    if (resourceData == nullptr || resourceSize == 0)
        return E_FAIL;

    // 2. 初始化当前线程的 COM，供 WIC 图片解码使用。
    HRESULT hr = CoInitializeEx(
        nullptr,
        COINIT_APARTMENTTHREADED
    );

    if (FAILED(hr))
        return hr;

    // 后面创建的 ComPtr 会先释放，最后再清理 COM。
    // S_FALSE 也表示成功增加 COM 初始化计数，仍需配对 CoUninitialize；初始化失败时不创建守卫。
    ComScope comScope;

    ComPtr<IWICImagingFactory> factory;

    hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.GetAddressOf())
    );

    if (FAILED(hr))
        return hr;

    // 3. 将 EXE 中的 PNG 数据交给 WIC。
    // 资源内存随 EXE 保持有效，本函数仅用于读取。
    ComPtr<IWICStream> stream;

    hr = factory->CreateStream(stream.GetAddressOf());
    if (FAILED(hr))
        return hr;

    hr = stream->InitializeFromMemory(resourceData, resourceSize);
    if (FAILED(hr))
        return hr;

    ComPtr<IWICBitmapDecoder> decoder;

    hr = factory->CreateDecoderFromStream(
        stream.Get(),
        nullptr,
        WICDecodeMetadataCacheOnLoad,
        decoder.GetAddressOf()
    );

    if (FAILED(hr))
        return hr;

    ComPtr<IWICBitmapFrameDecode> frame;

    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr))
        return hr;

    // 4. 转换为 RGBA：每个像素包含红、绿、蓝、透明度。
    ComPtr<IWICFormatConverter> converter;

    hr = factory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr))
        return hr;

    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom
    );

    if (FAILED(hr))
        return hr;

    UINT width = 0;
    UINT height = 0;

    hr = converter->GetSize(&width, &height);
    if (FAILED(hr))
        return hr;

    // 当前加载器面向 UI 图片，限制尺寸，避免异常大内存分配。
    if (width == 0 || height == 0 ||
        width > 4096 || height > 4096)
    {
        return E_INVALIDARG;
    }

    // 此前的 4096 尺寸限制同时保证乘法适合 UINT，符合 WIC CopyPixels 的字节数接口。
    const UINT rowBytes = width * 4;
    const UINT totalBytes = rowBytes * height;

    std::vector<BYTE> pixels(totalBytes);

    hr = converter->CopyPixels(
        nullptr,
        rowBytes,
        totalBytes,
        pixels.data()
    );

    if (FAILED(hr))
        return hr;

    // 5. 把像素上传到 DX11 纹理。
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    // 图标加载后不再修改，采用单层不可变纹理；像素缓冲只需存活到 CreateTexture2D 返回。
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initialData{};
    initialData.pSysMem = pixels.data();
    initialData.SysMemPitch = rowBytes;

    ComPtr<ID3D11Texture2D> texture;

    hr = device->CreateTexture2D(
        &description,
        &initialData,
        texture.GetAddressOf()
    );

    if (FAILED(hr))
        return hr;

    // 6. 创建 ImGui 的 DX11 后端需要的纹理视图。
    hr = device->CreateShaderResourceView(
        texture.Get(),
        nullptr,
        image.view.GetAddressOf()
    );

    if (FAILED(hr))
        return hr;

    // 成功的视图已经持有底层纹理引用，局部 texture 释放不会使返回结果失效。
    image.width = width;
    image.height = height;

    return S_OK;
}