/**
 * @file serial_history.h
 * @brief 主线程历史缓存、显示冻结及独立 I/O 统计。
 * 记录边界由读取和合并策略决定，不能用于推断设备协议帧边界。
 */
#pragma once

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// 一条记录代表一次发送或连续读取的数据，不代表协议中的完整数据帧。
struct SerialDataRecord
{
    SYSTEMTIME time = {}; // 工作线程生成收发记录时采集的本机时间。
    bool has_timestamp = false; // 生成时确定，开关不追溯历史。
    bool error = false;
    bool display_only = false; // 原始字节已释放，保留当时的显示文本。
    bool visible = true; // 生成时确定，Rx/Tx 开关不追溯历史。
    bool transmit = false;
    bool hex = true; // 发送时的模式，用于合并判断；显示仍遵循输出区模式。
    std::vector<unsigned char> bytes;
    std::string display;
};

// 主线程历史模型。工作线程只传递 SerialDataRecord，不访问该对象。
class SerialHistory
{
public:
    static constexpr std::size_t ByteLimit = 256 * 1024;
    static constexpr std::size_t RecordLimit = 2048;

    /// @brief 接管一条记录，满足属性和时间条件时合并尾记录，再按限额裁剪。
    void Append(SerialDataRecord record);
    /// @brief 追加受长度限制的错误文本，并根据参数采集本机时间。
    void AppendError(std::string text, bool timestamp);
    // 实际 I/O 计数独立于历史缓存；裁剪、隐藏记录不减计数。
    void AddTransferCounts(std::uint64_t received, std::uint64_t written);
    std::uint64_t ReceivedBytes() const { return received_; }
    std::uint64_t WrittenBytes() const { return written_; }
    std::size_t RawByteCount() const { return raw_bytes_; }
    // 返回只读借用；遍历期间不得执行追加、清除等会改变模型的操作。
    const std::deque<SerialDataRecord>& Records() const { return records_; }

    // 清原始缓存和计数，保留当前文字及可见性；旧文字不再切换格式。
    void ClearRawData(bool display_hex);
    // 隐藏现有文字，保留原始缓存和计数；后续记录仍按自身可见性显示。
    void HideText();
    void InvalidateDisplay() { dirty_ = true; }
    // 更新可见记录的显示缓存，返回是否有变化，供 UI 决定滚动行为。
    bool RefreshDisplay(bool display_hex);

private:
    /// @brief 从最旧记录开始裁剪至两个限额内；不回减实际 I/O 计数。
    void Trim();
    std::deque<SerialDataRecord> records_;
    std::size_t raw_bytes_ = 0;
    std::uint64_t received_ = 0;
    std::uint64_t written_ = 0;
    // 记录或显示选项变化置脏，RefreshDisplay 消费；隐藏与已冻结记录不重新格式化。
    bool dirty_ = true;
    bool display_hex_ = true;
};
