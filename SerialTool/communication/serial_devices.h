/**
 * @file serial_devices.h
 * @brief 端口枚举和设备描述查询接口。
 * 查询不取得串口通信所有权；枚举得到的端口不保证随后一定能够打开。
 */
#pragma once

#include <Windows.h>
#include <string>
#include <vector>

// 串口设备的只读说明；identifier 是设备实例标识，不保证是硬件序列号。
struct SerialDeviceInfo
{
    std::wstring description;
    std::wstring manufacturer;
    std::wstring identifier;
};
/// @brief 按 COM 名称查询属性；未找到或属性不可用时相应字段为空。
SerialDeviceInfo QuerySerialDeviceInfo(const std::wstring& portName);

// 枚举不打开串口；失败时 ports 为空，error 为 Windows 错误码。
struct SerialPortList
{
    std::vector<std::string> ports;
    DWORD error = ERROR_SUCCESS;
};
/// @brief 返回数字排序后的 COM 名称，错误通过结果中的 error 交付。
SerialPortList EnumerateSerialPorts();
