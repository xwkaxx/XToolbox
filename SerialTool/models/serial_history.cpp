/**
 * @file serial_history.cpp
 * @brief 主线程历史模型：接纳记录、限制缓存并维护独立的实际 I/O 计数。
 * 显示文本是缓存；释放原始数据后冻结的文字不再参与格式转换。
 */
#include "serial_history.h"
#include "serial_codec.h"

#include <utility>

/// @brief 接管一条记录，按时间与属性合并相邻数据，随后满足字节和条数上限。
void SerialHistory::Append(SerialDataRecord record)
{
    raw_bytes_ += record.bytes.size();
    bool merge = false;
    if (!records_.empty())
    {
        const auto& previous = records_.back();
        // 错误、冻结文本和可见性不同的记录不能合并，否则清空操作及颜色边界会被破坏。
        const bool sameDirection = !previous.error && !record.error
            && !previous.display_only && !record.display_only
            && previous.visible == record.visible
            && previous.transmit == record.transmit
            && (!record.transmit || previous.hex == record.hex);
        const auto& a = previous.time;
        const auto& b = record.time;
        // 同毫秒的连续读取合并；比较日期，避免跨天合并。
        const bool sameTime = previous.has_timestamp && record.has_timestamp
            && a.wYear == b.wYear && a.wMonth == b.wMonth && a.wDay == b.wDay
            && a.wHour == b.wHour && a.wMinute == b.wMinute
            && a.wSecond == b.wSecond && a.wMilliseconds == b.wMilliseconds;
        merge = sameDirection && sameTime;
    }
    if (merge)
    {
        auto& previous = records_.back();
        previous.bytes.insert(previous.bytes.end(), record.bytes.begin(), record.bytes.end());
    }
    else
        records_.push_back(std::move(record));
    Trim();
    dirty_ = true;
}

/// @brief 从最旧记录开始裁剪，不改变 received_/written_ 实际收发计数。
void SerialHistory::Trim()
{
    while (records_.size() > RecordLimit || raw_bytes_ > ByteLimit)
    {
        auto& first = records_.front();
        const std::size_t excess = raw_bytes_ > ByteLimit ? raw_bytes_ - ByteLimit : 0;
        // 只有字节超限且不需要删除整条时才裁剪前缀；条数超限必须整条移除。
        if (records_.size() <= RecordLimit && excess < first.bytes.size())
        {
            first.bytes.erase(first.bytes.begin(), first.bytes.begin() + excess);
            raw_bytes_ -= excess;
            break;
        }
        raw_bytes_ -= first.bytes.size();
        records_.pop_front();
    }
}

/// @brief 将非空提示作为独立错误记录写入，限制单条文本长度。
void SerialHistory::AppendError(std::string text, bool timestamp)
{
    if (text.empty()) return;
    SerialDataRecord record;
    record.has_timestamp = timestamp;
    if (timestamp) ::GetLocalTime(&record.time);
    record.error = true;
    // 错误文本没有原始字节，不计入 ByteLimit，因此额外限制文本长度并依赖条数上限。
    if (text.size() > 4096) text.resize(4096);
    record.display = std::move(text);
    Append(std::move(record));
}

/// @brief 累加会话提供的实际 I/O 数量，与历史记录是否可见或已裁剪无关。
void SerialHistory::AddTransferCounts(std::uint64_t received, std::uint64_t written)
{
    received_ += received;
    written_ += written;
}

/// @brief 将旧记录转为仅显示状态并归零计数，保留可见性与时间戳。
void SerialHistory::ClearRawData(bool display_hex)
{
    for (auto& record : records_)
    {
        if (!record.error && !record.display_only)
            record.display = SerialCodec::FormatReceived(record.bytes, display_hex);
        // 通过与空容器交换释放容量，单独 clear 只清长度，无法实现清缓存的内存语义。
        std::vector<unsigned char>().swap(record.bytes);
        record.display_only = true;
    }
    raw_bytes_ = 0;
    received_ = written_ = 0;
    dirty_ = true;
}

/// @brief 隐藏当前所有记录，不释放数据、不重置计数，也不影响未来记录。
void SerialHistory::HideText()
{
    for (auto& record : records_)
        record.visible = false;
    dirty_ = true;
}

/// @brief 在数据或模式改变时重建可见记录显示缓存，返回是否刷新过。
bool SerialHistory::RefreshDisplay(bool display_hex)
{
    if (!dirty_ && display_hex_ == display_hex) return false;
    for (auto& record : records_)
    {
        // 错误和冻结文字保留原显示；已隐藏数据不重新格式化，也不会因切换模式重新显示。
        if (record.error || record.display_only || !record.visible) continue;
        record.display = SerialCodec::FormatReceived(record.bytes, display_hex);
    }
    display_hex_ = display_hex;
    dirty_ = false;
    return true;
}
