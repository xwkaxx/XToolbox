/**
 * @file main.cpp
 * @brief 串口助手的 Win32/DX11 宿主。
 * 基于 Dear ImGui 示例；本文件负责平台生命周期，具体界面由独立 UI 模块绘制。
 */
// Dear ImGui: standalone example application for Windows API + DirectX 11

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>

#include <windows.h>
#include <dwmapi.h>

// Windows 桌面窗口管理器，用于设置原生窗口圆角。
#pragma comment(lib, "dwmapi.lib")
#include <filesystem>
#include <string>
#include <system_error>

#include "ui/serial_ui.h"
#include "ui/serial_window.h"

// Data
// 本模块独占这些 COM 引用，均由主线程访问；窗口尺寸只在消息处理器中登记。
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static bool                     g_SwapChainOccluded = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
bool CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Main code
/// @brief 串口助手入口：建立窗口和图形资源，运行主循环，再按依赖逆序清理。
int main(int, char**)
{
    // Make process DPI aware and obtain main monitor scale
    // DPI 感知必须在创建窗口前启用，确保窗口尺寸与后续命中坐标使用一致的像素体系。
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    // Create application window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    if (::RegisterClassExW(&wc) == 0)
        return 1;
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"SerialTool", WS_OVERLAPPEDWINDOW, 100, 100, (int)(1280 * main_scale), (int)(800 * main_scale), nullptr, nullptr, wc.hInstance, nullptr);

    if (hwnd == nullptr)
    {
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // 触发非客户区重新计算，让自定义标题栏替代系统标题栏。
    // 保留窗口原有的位置、大小和层级。
    ::SetWindowPos(
        hwnd,
        nullptr,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
        SWP_NOACTIVATE | SWP_FRAMECHANGED);

    // 请求 Windows 11 原生圆角，系统负责边缘抗锯齿和最大化时的直角处理。
    // Windows 10 不支持此属性，失败时保留直角，不影响程序运行。
    const DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
    const HRESULT cornerResult = ::DwmSetWindowAttribute(
        hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
        &cornerPreference, sizeof(cornerPreference));
    if (FAILED(cornerResult))
        ::OutputDebugStringW(L"SerialTool: 系统未应用原生窗口圆角。\n");
    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::DestroyWindow(hwnd);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    // TODO:暂时不保存布局，避免与工具箱共用默认的 imgui.ini。
    io.IniFilename = nullptr;

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
    //io.ConfigViewportsNoAutoMerge = true;
    //io.ConfigViewportsNoTaskBarIcon = true;
    //io.ConfigDockingAlwaysTabBar = true;
    //io.ConfigDockingTransparentPayload = true;

    // Setup Dear ImGui style
    //ImGui::StyleColorsDark();
    ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();

    // 先设置基础尺寸，再统一应用屏幕 DPI 缩放。
    // 这些设置只在初始化时执行。
    style.FramePadding = ImVec2(8.0f, 6.0f);      // 控件内部留白
    style.ItemSpacing = ImVec2(12.0f, 10.0f);    // 控件之间的间距
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f); // 控件与其标签的间距
    style.FrameRounding = 3.0f;                 // 控件圆角
    style.FrameBorderSize = 1.0f;               // 输入框等控件的边框
    style.ScrollbarSize = 12.0f;                // 滚动条宽度

    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)
    io.ConfigDpiScaleFonts = true;          // [Experimental] Automatically overwrite style.FontScaleDpi in Begin() when Monitor DPI changes. This will scale fonts but _NOT_ scale sizes/padding for now.
    io.ConfigDpiScaleViewports = true;      // [Experimental] Scale Dear ImGui and Platform Windows when Monitor DPI changes.

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    // 后端依赖原生窗口与 DX11 设备；销毁时先关后端，最后释放底层对象。
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // DX11 设备已创建，可以加载界面图片。
    // 此阶段设备可用，图标加载失败由 UI 层回滚资源；主程序保留简化图形继续运行。
    const HRESULT iconResult = SerialTool_Init(g_pd3dDevice);

    if (FAILED(iconResult))
    {
        const std::wstring message =
            std::wstring(L"图标加载失败，将使用简化图形。\nHRESULT（十进制）：")
            + std::to_wstring(static_cast<unsigned long>(iconResult));

        MessageBoxW(
            hwnd,
            message.c_str(),
            L"SerialTool",
            MB_OK | MB_ICONWARNING
        );
    }

    // Load Fonts
    // - If fonts are not explicitly loaded, Dear ImGui will select an embedded font: either AddFontDefaultVector() or AddFontDefaultBitmap().
    //   This selection is based on (style.FontSizeBase * style.FontScaleMain * style.FontScaleDpi) reaching a small threshold.
    // - You can load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - If a file cannot be loaded, AddFont functions will return a nullptr. Please handle those errors in your code (e.g. use an assertion, display an error and quit).
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use FreeType for higher quality font rendering.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //style.FontSizeBase = 20.0f;
    //io.Fonts->AddFontDefaultVector();
    //io.Fonts->AddFontDefaultBitmap();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);

    // 设置基础字号，屏幕缩放继续由已有的 DPI 配置处理。
    style.FontSizeBase = 20.0f;

    // 获取当前 EXE 的完整路径。
    std::wstring executablePath(32768, L'\0');

    const DWORD pathLength = GetModuleFileNameW(
        nullptr,
        executablePath.data(),
        static_cast<DWORD>(executablePath.size())
    );

    ImFont* applicationFont = nullptr;

    if (pathLength > 0 && pathLength < executablePath.size())
    {
        executablePath.resize(pathLength);

        // 从 EXE 所在目录寻找字体，而不是从当前工作目录寻找。
        const std::filesystem::path fontPath =
            std::filesystem::path(executablePath).parent_path()
            / "assets" / "fonts" / "SourceHanSansCN-Regular.otf";

        std::error_code error;

        if (std::filesystem::is_regular_file(fontPath, error))
        {
            // 当前项目使用 C++17，u8string() 返回 std::string。
            const std::string fontPathUtf8 = fontPath.u8string();

            applicationFont =
                io.Fonts->AddFontFromFileTTF(fontPathUtf8.c_str());
        }
    }

    if (applicationFont != nullptr)
    {
        io.FontDefault = applicationFont;
    }
    else
    {
        // 字体未找到时保留基本界面，并提示检查资源目录。
        io.FontDefault = io.Fonts->AddFontDefault();

        MessageBoxW(
            hwnd,
            L"字体加载失败，请检查 EXE 旁的 assets/fonts 目录。",
            L"SerialTool",
            MB_OK | MB_ICONWARNING
        );
    }

    // 窗口背景清除色。
    ImVec4 clear_color = ImVec4(0.95f, 0.96f, 0.97f, 1.00f);

    // Main loop
    int exitCode = 0;
    bool done = false;
    while (!done)
    {
        // Poll and handle messages (inputs, window resize, etc.)
        // See the WndProc() function below for our to dispatch events to the Win32 backend.
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Handle window being minimized or screen locked
        // 遮挡时只探测是否恢复显示并短暂让出 CPU，避免持续生成不可见帧。
        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        // Handle window resize (we don't resize directly in the WM_SIZE handler)
        // 合并本轮收到的尺寸变化，在帧间处理，避免窗口消息重入期间替换正在使用的渲染目标。
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            // 先解除设备上下文对旧渲染目标的引用，再释放并调整交换链。
            g_pd3dDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
            CleanupRenderTarget();
            const HRESULT resizeResult = g_pSwapChain->ResizeBuffers(
                0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            if (FAILED(resizeResult) || !CreateRenderTarget())
            {
                MessageBoxW(hwnd, L"调整窗口渲染缓冲区失败，程序将退出。",
                    L"SerialTool", MB_OK | MB_ICONERROR);
                exitCode = 1;
                break;
            }
        }

        // Start the Dear ImGui frame
        // 先更新平台与渲染后端，再开始 ImGui 帧；业务绘制必须位于 NewFrame 与 Render 之间。
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // 每帧绘制串口助手界面，包括配置面板、接收区和发送区。
        // serial_ui.cpp 组合面板并消费会话事件，实际串口 I/O 在会话工作线程执行。
        SerialTool_Draw();

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Update and Render additional Platform Windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            // 多视口窗口有独立的交换链，由后端负责更新，不能只渲染主窗口。
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        // Present
        HRESULT hr = g_pSwapChain->Present(1, 0);   // Present with vsync
        //HRESULT hr = g_pSwapChain->Present(0, 0); // Present without vsync
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    // Cleanup
    // 先等待通信线程结束并释放图标，再关闭 ImGui 和 DX11，避免后台或面板访问已销毁对象。
    SerialTool_Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return exitCode;
}

// Helper functions
/// @brief 建立 DX11 设备、上下文和交换链；失败后调用者仍需执行 CleanupDeviceD3D。
bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    // This is a basic setup. Optimally could use e.g. DXGI_SWAP_EFFECT_FLIP_DISCARD and handle fullscreen mode differently. See #8979 for suggestions.
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    // 仅硬件能力不支持时回退 WARP；其他失败交给上层清理并结束初始化。
    if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    // Disable DXGI's default Alt+Enter fullscreen behavior.
    // - You are free to leave this enabled, but it will not work properly with multiple viewports.
    // - This must be done for all windows associated to the device. Our DX11 backend does this automatically for secondary viewports that it creates.
    IDXGIFactory* pSwapChainFactory;
    if (SUCCEEDED(g_pSwapChain->GetParent(IID_PPV_ARGS(&pSwapChainFactory))))
    {
        pSwapChainFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);
        pSwapChainFactory->Release();
    }

    return CreateRenderTarget();
}

/// @brief 释放渲染目标及 DX11 所有者引用并置空，允许部分初始化后的重复清理。
void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

/// @brief 从交换链后缓冲建立渲染目标视图；初始化和窗口尺寸变化后调用。
bool CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    const HRESULT bufferResult = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(bufferResult))
        return false;

    const HRESULT viewResult = g_pd3dDevice->CreateRenderTargetView(
        backBuffer, nullptr, &g_mainRenderTargetView);
    // GetBuffer 取得的是额外 COM 引用；无论视图创建是否成功，都必须在这里释放。
    backBuffer->Release();
    return SUCCEEDED(viewResult);
}
/// @brief 释放应用持有的目标视图引用，为调整交换链或退出准备。
void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
/// @brief 分派窗口输入及生命周期消息；尺寸变化只登记，图形资源在主循环重建。
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // 优先处理标题栏区域和窗口边缘的命中测试。
    // 此处理函数不依赖 ImGui 初始化，创建窗口期间也可以调用。
    LRESULT result = 0;
    if (SerialTool_HandleWindowMessage(
        hWnd, msg, wParam, lParam, &result))
    {
        return result;
    }

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        // 忽略最小化尺寸，保留最新有效尺寸供主循环一次性消费。
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_CLOSE:
        // 先退出渲染循环，统一释放图形资源后再销毁窗口。
        ::PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
