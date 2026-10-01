/**
 * @file serial_session.h
 * @brief 主线程与同步 I/O 工作线程之间的会话边界。
 * 请求按值传递，结果批量取走；调用方不得把公开方法当成可任意并发调用的接口。
 */
#pragma once

#include "serial_port.h"
#include "../models/serial_history.h"
#include <memory>

// 同步传输接口：Open/Close 在主线程，Read/Write 在唯一工作线程。
// Read 必须有界等待；Close 只在线程退出后调用。默认实现使用 SerialPort。
class SerialTransport
{
public:
    virtual ~SerialTransport() = default;
    virtual bool Open(const SerialConfig& config) = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;
    virtual bool Write(const void* data, std::size_t size, std::size_t& written) = 0;
    virtual bool Read(void* data, std::size_t capacity, std::size_t& read) = 0;
    virtual const std::wstring& Error() const = 0;
};

/// @brief 一次发送的不可变输入快照；bytes 已由上层追加所需后缀。
struct SerialSendRequest
{
    std::vector<unsigned char> bytes;
    bool hex = true;
};

/// @brief 调度状态的瞬时副本，仅用于展示，提交仍需重新检查状态。
struct SerialSessionSnapshot
{
    bool connected = false;
    // connected 只反映句柄是否仍打开；工作线程失败后、主线程关闭前，ready 可先变为 false。
    bool ready = false;
    bool timed_active = false;
    int timed_total = 0;
    std::uint64_t timed_completed = 0;
};

/// @brief 上次 Poll 以来的一批增量结果；不是整个会话的累计历史。
struct SerialSessionEvents
{
    std::deque<SerialDataRecord> records;
    std::deque<std::wstring> notices;
    std::uint64_t received = 0;
    std::uint64_t written = 0;
    // 队列限额造成的丢弃字节数；实际 I/O 计数仍包含这些被丢弃的字节。
    std::size_t dropped = 0;
    bool disconnected_on_error = false;
};

// 所有公开操作只在主线程调用；锁、原子量和线程均封装在内部。
class SerialSession
{
public:
    SerialSession();
    explicit SerialSession(std::unique_ptr<SerialTransport> transport);
    ~SerialSession();
    SerialSession(const SerialSession&) = delete;
    SerialSession& operator=(const SerialSession&) = delete;

    /// @brief 打开并启动线程；失败通过 error 返回原因，已连接时拒绝重复打开。
    bool Open(const SerialConfig& config, std::wstring& error);
    // 先置停止标记、取消同步 I/O、join，最后关闭句柄。未消费事件保留。
    void Close();
    /// @brief true 表示成功入队；实际写出字节和失败通知从 Poll 获取。
    bool SendOnce(SerialSendRequest request, std::wstring& error);
    /// @brief 启动新一轮定时任务；interval_ms 为写完后的延时，repeat_count=-1 表示无限次。
    bool StartTimedSend(SerialSendRequest request, int interval_ms, int repeat_count,
        std::wstring& error);
    // 已开始的一次写入可完成；旧任务不得更新新一轮进度。
    void StopTimedSend();
    /// @brief 设置未来记录的属性；不追溯修改已经生成的事件。
    void SetRecordOptions(bool timestamp, bool show_receive, bool show_send);
    SerialSessionSnapshot Snapshot() const;
    // 失败时完成关闭并报告一次断开事件，批量取走收发结果。
    SerialSessionEvents Poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
