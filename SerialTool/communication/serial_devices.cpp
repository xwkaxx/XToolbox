/**
 * @file serial_devices.cpp
 * @brief 只读设备发现，不打开串口通信句柄。
 * SetupAPI 查询设备属性，QueryDosDevice 枚举可选端口；失败结果由调用者决定如何呈现。
 */
#include "serial_devices.h"

#include <SetupAPI.h>
#include <algorithm>
#include <utility>
#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "Advapi32.lib")

/// @brief 查找与端口名匹配的当前设备；缺失属性保持为空，不将实例标识当成可靠硬件序列号。
SerialDeviceInfo QuerySerialDeviceInfo(const std::wstring& portName)
{
    SerialDeviceInfo result;
    if (portName.empty()) return result;
    // Ports 安装类；只查询当前存在的设备，不打开通信句柄。
    const GUID portsClass = { 0x4d36e978, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };
    const HDEVINFO devices = ::SetupDiGetClassDevsW(&portsClass, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) return result;
    SP_DEVINFO_DATA device = {};
    device.cbSize = sizeof(device);
    for (DWORD index = 0; ::SetupDiEnumDeviceInfo(devices, index, &device); ++index)
    {
        // 逐设备读取 PortName 做匹配，而不是从友好名称中猜测端口号；注册表句柄在本轮内释放。
        const HKEY key = ::SetupDiOpenDevRegKey(devices, &device, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
        if (key == INVALID_HANDLE_VALUE) continue;
        wchar_t port[256] = {};
        DWORD type = 0, size = sizeof(port);
        const LSTATUS status = ::RegQueryValueExW(key, L"PortName", nullptr, &type,
            reinterpret_cast<BYTE*>(port), &size);
        ::RegCloseKey(key);
        port[255] = L'\0';
        if (status != ERROR_SUCCESS || type != REG_SZ || _wcsicmp(port, portName.c_str()) != 0) continue;
        // 属性长度以字节为单位，先查询所需空间并限制上限，再额外预留终止字符。
        const auto property = [&](DWORD id) -> std::wstring {
            DWORD required = 0, propertyType = 0;
            ::SetupDiGetDeviceRegistryPropertyW(devices, &device, id, &propertyType, nullptr, 0, &required);
            if (required == 0 || required > 65536) return {};
            std::vector<wchar_t> value(required / sizeof(wchar_t) + 1, L'\0');
            if (!::SetupDiGetDeviceRegistryPropertyW(devices, &device, id, &propertyType,
                reinterpret_cast<BYTE*>(value.data()), required, nullptr) || propertyType != REG_SZ) return {};
            return std::wstring(value.data());
        };
        result.description = property(SPDRP_DEVICEDESC);
        // 部分驱动不提供标准描述，友好名称仅作为显示回退，不影响端口匹配。
        if (result.description.empty()) result.description = property(SPDRP_FRIENDLYNAME);
        const std::wstring suffix = L" (" + portName + L")";
        if (result.description.size() >= suffix.size() &&
            result.description.compare(result.description.size() - suffix.size(), suffix.size(), suffix) == 0)
            result.description.resize(result.description.size() - suffix.size());
        result.manufacturer = property(SPDRP_MFG);
        DWORD required = 0;
        ::SetupDiGetDeviceInstanceIdW(devices, &device, nullptr, 0, &required);
        if (required > 0 && required < 32768)
        {
            std::vector<wchar_t> value(required + 1, L'\0');
            if (::SetupDiGetDeviceInstanceIdW(devices, &device, value.data(), required, nullptr))
            {
                const std::wstring instance(value.data());
                const size_t slash = instance.find_last_of(L'\\');
                result.identifier = slash == std::wstring::npos ? instance : instance.substr(slash + 1);
                // FTDI 的设备标识位于 FTDIBUS 中间段的最后一个加号之后。
                if (instance.compare(0, 8, L"FTDIBUS\\") == 0 && slash != std::wstring::npos)
                {
                    const size_t plus = instance.rfind(L'+', slash);
                    if (plus != std::wstring::npos)
                        result.identifier = instance.substr(plus + 1, slash - plus - 1);
                }
            }
        }
        break;
    }
    // 设备信息集不是普通文件句柄，必须使用 SetupAPI 对应的销毁函数。
    ::SetupDiDestroyDeviceInfoList(devices);
    return result;
}

// 查询系统设备名称，筛选 COM + 数字，不尝试打开设备，因此不占用串口。
/// @brief 返回按数字顺序排列且去重的 COM 名称；枚举失败携带 Win32 错误码。
SerialPortList EnumerateSerialPorts()
{
    SerialPortList result;
    std::vector<wchar_t> buffer(4096);
    DWORD length = 0;
    for (;;)
    {
        length = ::QueryDosDeviceW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length != 0)
            break;
        const DWORD error = ::GetLastError();
        if (error != ERROR_INSUFFICIENT_BUFFER || buffer.size() >= 1024 * 1024)
        {
            result.error = error;
            return result;
        }
        // 设备数量未知，按需扩容；达到上限或遇到其他错误时直接返回，避免无限重试。
        buffer.resize(buffer.size() * 2);
    }

    std::vector<std::string> ports;
    // 返回值是由多个以零结尾的名称组成的列表，末尾再追加一个零。
    for (size_t offset = 0; offset < length && buffer[offset] != L'\0';)
    {
        const std::wstring name(buffer.data() + offset);
        offset += name.size() + 1;
        if (name.size() <= 3 || name.compare(0, 3, L"COM") != 0)
            continue;
        if (!std::all_of(name.begin() + 3, name.end(),
            [](wchar_t c) { return c >= L'0' && c <= L'9'; }))
            continue;
        std::string port;
        for (wchar_t c : name)
            port.push_back(static_cast<char>(c)); // 已确认只包含 ASCII 字符。
        ports.push_back(port);
    }
    // 按端口编号排列，避免 COM10 出现在 COM2 前面。
    std::sort(ports.begin(), ports.end(), [](const std::string& a, const std::string& b)
    {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    });
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    result.ports = std::move(ports);
    return result;
}

