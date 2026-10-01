/**
 * @file serial_port.h
 * @brief 单个 Win32 串口句柄的独占同步封装。
 * 不保证线程安全；Read/Write 由会话的唯一工作线程调用，Close 必须等待该线程退出。
 */
#pragma once

#include <Windows.h>
#include <string>
#include <cstddef>

// 打开串口时使用的参数。
// 默认值与当前界面一致：115200、8 数据位、无校验、1 停止位。
struct SerialConfig
{
    std::wstring port_name;        // 例如 L"COM3"，未选择时为空
    DWORD baud_rate = 115200;
    BYTE data_bits = 8;
    // parity/stop_bits 使用 Windows DCB 常量，不能直接传入界面选项下标。
    BYTE parity = NOPARITY;
    BYTE stop_bits = ONESTOPBIT;
};

// 管理一个串口连接及其句柄。
class SerialPort
{
public:
    SerialPort() = default;
    ~SerialPort();

    // 禁止复制，避免两个对象管理同一个句柄并重复关闭。
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    // 打开端口并应用配置；全部成功才返回 true。
    bool Open(const SerialConfig& config);

    // 关闭串口；未打开时调用也安全。
    void Close();

    // 查询当前是否持有已打开的串口句柄。
    bool IsOpen() const;

    // 同步发送原始字节，不自动添加换行或字符串结束符。
    // 仅全部写入才返回 true；实际写入量由 bytes_written 返回。
    // 失败后不自动重发；当前类不支持多个线程并发操作。
    bool Write(const void* data, std::size_t size, std::size_t& bytes_written);
    // 同步读取。超时且没有数据也属于成功，此时 bytes_read 为 0。
    // 与 Write 一样仅由一个工作线程调用；关闭前必须等待工作线程结束。
    bool Read(void* buffer, std::size_t capacity, std::size_t& bytes_read);
    // 获取最近一次操作失败的说明。
    // 借用返回引用；下一次操作可能改写内容，需要跨操作保存时由调用方复制。
    const std::wstring& GetLastErrorMessage() const;

private:
    // INVALID_HANDLE_VALUE 表示未持有设备；失败回滚、主动关闭和析构共用此状态。
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::wstring last_error_message_;
};
