/**
 * @file serial_port.cpp
 * @brief Windows 同步串口 I/O 的最底层封装。
 * 不负责重试、线程调度或 UI；调用方负责串行访问及关闭前停止工作线程。
 */
#include "serial_port.h"

namespace
{
    /// @brief 将立即捕获的 Win32 错误码转换为带操作上下文的中文说明。
    std::wstring SerialSystemError(const wchar_t* operation, DWORD error)
    {
        std::wstring message;
        // 常见串口错误固定中文，其余错误使用 Windows 提供的说明。
        switch (error)
        {
        case ERROR_ACCESS_DENIED: message = L"拒绝访问。"; break;
        case ERROR_SHARING_VIOLATION: message = L"端口正被其他程序占用。"; break;
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND: message = L"找不到指定的串口设备。"; break;
        case ERROR_DEVICE_NOT_CONNECTED: message = L"设备未连接。"; break;
        default:
        {
            wchar_t* buffer = nullptr;
            const DWORD length = ::FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
            if (length && buffer) message.assign(buffer, length);
            if (buffer) ::LocalFree(buffer);
            while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
                message.pop_back();
            if (message.empty()) message = L"无法获取系统错误说明。";
            break;
        }
        }
        return std::wstring(operation) + L"失败：" + message
            + L"（错误码 " + std::to_wstring(error) + L"）";
    }
}

// 对象销毁时自动关闭串口，避免忘记释放句柄。
SerialPort::~SerialPort()
{
    Close();
}

// 关闭串口，允许重复调用。
void SerialPort::Close()
{
    if (handle_ == INVALID_HANDLE_VALUE)
        return;

    ::CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
}

// 判断当前对象是否持有串口句柄。
bool SerialPort::IsOpen() const
{
    return handle_ != INVALID_HANDLE_VALUE;
}

// 返回最近一次操作失败的说明，避免复制整个字符串。
const std::wstring& SerialPort::GetLastErrorMessage() const
{
    return last_error_message_;
}

/// @brief 独占打开端口并设置 DCB、超时；配置失败时释放本次取得的句柄。
bool SerialPort::Open(const SerialConfig& config)
{
    last_error_message_.clear();

    // 已打开时不直接覆盖句柄，必须先关闭。
    if (IsOpen())
    {
        last_error_message_ = L"串口已经打开，请先关闭。";
        return false;
    }

    if (config.port_name.empty())
    {
        last_error_message_ = L"请先选择串口。";
        return false;
    }

    // 保存错误并回滚本次取得的句柄；已打开的连接在前面单独拒绝，不能进入此路径。
    const auto fail = [this](const wchar_t* operation, DWORD error)
        {
            last_error_message_ = SerialSystemError(operation, error);

            Close();
            return false;
        };

    // 例如：COM3 转换为 \\.\COM3，也适用于 COM10 以上的端口。
    const std::wstring device_path = L"\\\\.\\" + config.port_name;

    handle_ = ::CreateFileW(
        device_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,                  // 独占访问，不与其他程序共享串口
        nullptr,
        OPEN_EXISTING,      // 打开已有设备
        0,                  // 当前先使用同步 I/O
        nullptr
    );

    if (handle_ == INVALID_HANDLE_VALUE)
        return fail(L"打开串口", ::GetLastError());

    // 以驱动当前 DCB 为基础，显式覆盖本应用控制的字段；枚举值不能直接来自 UI 下标。
    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);

    if (!::GetCommState(handle_, &dcb))
        return fail(L"读取串口配置", ::GetLastError());

    dcb.BaudRate = config.baud_rate;
    dcb.ByteSize = config.data_bits;
    dcb.Parity = config.parity;
    dcb.StopBits = config.stop_bits;

    dcb.fBinary = TRUE;
    dcb.fParity = (config.parity != NOPARITY);

    // 当前阶段不使用硬件流控或软件流控。
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fTXContinueOnXoff = TRUE;

    // DTR、RTS 暂不主动置为有效，后续需要时再提供界面控制。
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;

    // 保留原始接收字节，不丢弃零字节或替换错误字符。
    dcb.fNull = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fAbortOnError = FALSE;
    dcb.XonChar = 0x11;
    dcb.XoffChar = 0x13;

    if (!::SetCommState(handle_, &dcb))
        return fail(L"应用串口配置", ::GetLastError());

    // 超时单位为毫秒。同步读取需有限等待，以周期性检查停止标记并调度发送；写入也必须有界。
    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 30;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 1000;

    if (!::SetCommTimeouts(handle_, &timeouts))
        return fail(L"设置串口超时", ::GetLastError());

    // 所有步骤成功，保留句柄供后续收发使用。
    return true;
}
// 同步写入使用 Open() 设置的超时，由 SerialSession 的唯一工作线程调用。
bool SerialPort::Write(const void* data, std::size_t size, std::size_t& bytes_written)
{
    bytes_written = 0;
    last_error_message_.clear();

    if (!IsOpen())
    {
        last_error_message_ = L"串口尚未打开，无法发送。";
        return false;
    }

    // 空数据视为成功的无操作，不调用 Windows API。
    if (size == 0)
        return true;

    if (data == nullptr)
    {
        last_error_message_ = L"发送数据指针为空。";
        return false;
    }

    // WriteFile 使用 DWORD 表示长度，转换前检查，避免 64 位长度被截断。
    if (size > static_cast<std::size_t>(MAXDWORD))
    {
        last_error_message_ = L"单次发送数据过大，超过 Windows 写入长度上限。";
        return false;
    }

    DWORD written = 0;
    const BOOL succeeded = ::WriteFile(
        handle_, data, static_cast<DWORD>(size), &written, nullptr);
    // 失败后立即获取错误码，避免被后续函数调用覆盖。
    const DWORD error = succeeded ? ERROR_SUCCESS : ::GetLastError();
    bytes_written = written;

    if (!succeeded)
    {
        last_error_message_ = SerialSystemError(L"写入串口", error)
            + L"；系统报告写入 " + std::to_wstring(bytes_written)
            + L" / " + std::to_wstring(size) + L" 字节。";
        // API 失败并不保证对端未收到数据，不能据此自动重发整段。
        return false;
    }

    // 串口写超时可能返回 TRUE，但实际字节数少于请求值。
    if (bytes_written != size)
    {
        last_error_message_ = L"串口未完整写入（可能超时）："
            + std::to_wstring(bytes_written) + L" / "
            + std::to_wstring(size) + L" 字节；未自动重发。";
        return false;
    }

    // 表示本机写入完成，不等同于对端已接收、解析或执行。
    return true;
}
/// @brief 检查线路错误后读取原始字节；无数据超时成功返回，bytes_read 为零。
bool SerialPort::Read(void* buffer, std::size_t capacity, std::size_t& bytes_read)
{
    bytes_read = 0;
    last_error_message_.clear();
    if (!IsOpen())
    {
        last_error_message_ = L"串口尚未打开，无法接收。";
        return false;
    }
    if (capacity == 0)
        return true;
    if (buffer == nullptr || capacity > static_cast<std::size_t>(MAXDWORD))
    {
        last_error_message_ = L"接收缓冲区或长度无效。";
        return false;
    }

    // 线路错误与设备移除由上层会话统一断开；此处只记录原因，不自行重连或吞掉错误。
    DWORD errors = 0;
    COMSTAT status = {};
    if (!::ClearCommError(handle_, &errors, &status))
    {
        const DWORD error = ::GetLastError();
        last_error_message_ = SerialSystemError(L"查询串口状态", error);
        return false;
    }
    if (errors != 0)
    {
        last_error_message_ = L"串口线路错误，标志值：" + std::to_wstring(errors)
            + L"。请检查波特率、校验位及接线。";
        return false;
    }

    DWORD count = 0;
    if (!::ReadFile(handle_, buffer, static_cast<DWORD>(capacity), &count, nullptr))
    {
        const DWORD error = ::GetLastError();
        last_error_message_ = SerialSystemError(L"读取串口", error);
        return false;
    }
    // 保留驱动实际返回的长度，不添加 NUL；二进制数据可包含任意零字节。
    bytes_read = count;
    return true;
}
