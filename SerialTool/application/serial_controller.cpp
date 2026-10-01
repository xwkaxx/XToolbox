/**
 * @file serial_controller.cpp
 * @brief 串口助手的主线程应用协调层。
 * 连接、发送、配置和显示记录在此衔接；UI 不接触会话内部锁或队列。
 */
#include "serial_controller.h"
#include "../models/serial_codec.h"

#include <utility>

namespace
{
    /// @brief 将设备或驱动返回的 UTF-16 文本转换为 UI 使用的 UTF-8。
    std::string ToUtf8(const std::wstring& text)
    {
        if (text.empty()) return {};
        const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(),
            static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0) return "错误说明转换失败";
        std::string result(size, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
            result.data(), size, nullptr, nullptr);
        return result;
    }
}

/// @brief 校验 UI 选项并映射为 Win32 配置；失败不修改输出 config。
bool SerialConnectionOptions::BuildConfig(const std::string& port, SerialConfig& config) const
{
    static constexpr BYTE parities[] = { NOPARITY, ODDPARITY, EVENPARITY };
    static constexpr BYTE dataBits[] = { 5, 6, 7, 8 };
    static constexpr BYTE stopBits[] = { ONESTOPBIT, ONE5STOPBITS, TWOSTOPBITS };
    if (baud_index < 0 || baud_index > CustomBaudIndex
        || (baud_index == CustomBaudIndex && custom_baud == 0)
        || parity_index < 0 || parity_index >= 3
        || data_bits_index < 0 || data_bits_index >= 4
        || stop_bits_index < 0 || stop_bits_index >= 3)
        return false;
    // 使用临时配置直到全部下标检查通过，防止错误输入留下半更新状态。
    SerialConfig result;
    result.port_name.assign(port.begin(), port.end());
    result.baud_rate = baud_index == CustomBaudIndex ? custom_baud : BaudRates[baud_index];
    result.parity = parities[parity_index];
    result.data_bits = dataBits[data_bits_index];
    result.stop_bits = stopBits[stop_bits_index];
    config = std::move(result);
    return true;
}

/// @brief 接管注入的传输对象；其线程生命周期仍统一归 SerialSession 管理。
SerialController::SerialController(std::unique_ptr<SerialTransport> transport)
    : session_(std::move(transport)) {}

/// @brief 将主线程偏好发布给会话；只影响之后产生的记录。
void SerialController::ApplyRecordOptions()
{
    session_.SetRecordOptions(preferences_.show_timestamp,
        preferences_.show_receive, preferences_.show_send);
}

/// @brief 加载偏好并恢复编辑器，不自动连接或启动连续发送。
void SerialController::InitializePreferences()
{
    std::string error;
    if (!LoadSerialSettings(preferences_, error)) messages_ += "[错误] " + error + "\n";
    if (!editor_.Restore(preferences_.last_send_text, preferences_.send_hex))
        messages_ += "[发送] 已保存内容超出发送框容量。\n";
    ApplyRecordOptions();
    history_.InvalidateDisplay();
}

/// @brief 保存最后一次有效提交及当前选项，不用未提交的输入草稿覆盖历史内容。
void SerialController::SavePreferences()
{
    // 编辑器区分草稿与有效提交；配置只能读取有效提交接口。
    preferences_.last_send_text = editor_.LastSubmittedText();
    preferences_.send_hex = editor_.IsHex();
    ApplyRecordOptions();
    std::string error;
    if (!SaveSerialSettings(preferences_, error)) messages_ += "[错误] " + error + "\n";
}

/// @brief 校验参数并打开会话；失败说明进入主线程消息缓冲。
bool SerialController::Connect()
{
    SerialConfig config;
    if (!connection_.BuildConfig(preferences_.selected_port, config))
    {
        messages_ += "[连接] 串口参数选择无效，请重新选择。\n";
        return false;
    }
    ApplyRecordOptions();
    std::wstring error;
    if (!session_.Open(config, error))
    {
        messages_ += "[错误] " + preferences_.selected_port + " - " + ToUtf8(error) + "\n";
        return false;
    }
    return true;
}

/// @brief 主动断开并接收工作线程的最后一批结果，保留当前历史。
void SerialController::Disconnect()
{
    // Close 已等待线程结束，此后 Pump 能完整取走最后一次写入或读取的结果。
    session_.Close();
    Pump();
    history_.InvalidateDisplay();
}

/// @brief 结束会话供应用退出或重新初始化使用，不执行 UI 绘制。
void SerialController::Shutdown() { session_.Close(); }

/// @brief 每帧消费会话结果，更新实际计数、历史和错误提示。
void SerialController::Pump()
{
    auto events = session_.Poll();
    if (events.disconnected_on_error)
    {
        history_.InvalidateDisplay();
        messages_ += "[连接] 收发异常，连接已关闭。请检查设备后重新连接。\n";
    }
    // 计数来自会话实际 I/O，不从显示记录长度推导，因为队列可能已经裁剪旧记录。
    history_.AddTransferCounts(events.received, events.written);
    for (auto& record : events.records) history_.Append(std::move(record));
    for (const auto& notice : events.notices) messages_ += ToUtf8(notice) + "\n";
    if (events.dropped != 0)
        messages_ += "[显示] 缓冲区已满，丢弃 " + std::to_string(events.dropped)
            + " 字节的旧显示记录，收发计数保留。\n";
    FlushMessages();
}

/// @brief 解析输入、准备持久化文本并提交请求；Accepted 只代表入队成功。
SerialSendResult SerialController::Send()
{
    // 未连接属于 UI 提示场景，不追加错误日志，也不悄悄排队等待未来连接。
    if (!Status().ready) return SerialSendResult::NotConnected;
    std::vector<unsigned char> bytes;
    std::string error, saved;
    if (!editor_.ReadBytes(bytes, error))
    {
        messages_ += "[发送] " + error + "\n";
        return SerialSendResult::Rejected;
    }
    // 先验证内容能完整保存，再接受发送；失败不能更新最后一次有效提交。
    if (!editor_.MakeSavedText(bytes, saved, error))
    {
        messages_ += "[发送] 无法保存完整内容：" + error + "\n";
        return SerialSendResult::Rejected;
    }
    // 后缀只追加到发送副本，保存内容不包含后缀，避免连续发送或下次恢复时重复追加。
    SerialCodec::AppendSendSuffix(bytes, preferences_.send_append);
    SerialSendRequest request{ std::move(bytes), editor_.IsHex() };
    std::wstring ioError;
    const bool accepted = preferences_.timed_send
        ? session_.StartTimedSend(std::move(request), preferences_.send_interval_ms,
            preferences_.send_repeat_count, ioError)
        : session_.SendOnce(std::move(request), ioError);
    if (!accepted)
    {
        messages_ += "[发送] " + ToUtf8(ioError) + "\n";
        return SerialSendResult::Rejected;
    }
    // 仅在会话接受请求后提交记忆；驱动实际成功/失败由后续 Pump 的事件报告。
    editor_.RememberSubmitted(std::move(saved));
    SavePreferences(); // 会话已释放队列锁，文件保存不会阻塞工作线程取任务。
    return SerialSendResult::Accepted;
}

/// @brief 在需要显示发送区时进行一次延迟恢复，并将错误送入提示缓冲。
void SerialController::InitializeEditor()
{
    std::string error;
    if (!editor_.Initialize(error)) messages_ += "[发送] " + error + "\n";
}

/// @brief 通过编辑器执行事务式转换，仅转换成功后保存偏好。
void SerialController::ToggleSendMode()
{
    std::string error;
    if (!editor_.ToggleMode(error)) messages_ += "[发送] " + error + "\n";
    else SavePreferences();
}

/// @brief 清除草稿和记忆内容；不触及已提交的定时发送快照。
void SerialController::ClearSendText()
{
    editor_.Clear();
    SavePreferences();
}

/// @brief 先归并待消费结果，再清原始缓存与计数；后续 I/O 仍从零继续统计。
void SerialController::ClearRawData()
{
    Pump();
    history_.ClearRawData(preferences_.display_hex);
}

/// @brief 隐藏当前已产生的文字，保留原始数据和计数，不影响未来记录。
void SerialController::HideOutputText()
{
    Pump();
    history_.HideText();
    messages_.clear();
}

/// @brief 将累积提示合并成一条历史错误记录，并清空暂存消息。
void SerialController::FlushMessages()
{
    // 错误记录的时间戳在主线程刷新时确定，与工作线程捕获的数据记录时间不同。
    history_.AppendError(std::move(messages_), preferences_.show_timestamp);
    messages_.clear();
}

/// @brief 查询可选端口，不隐式改变所选端口或连接。
SerialPortList SerialController::RefreshPorts() const { return EnumerateSerialPorts(); }

/// @brief 按可用字段拼接设备摘要，忽略缺失字段和多余分隔符。
std::string SerialController::PortDetails() const
{
    const auto& port = preferences_.selected_port;
    const auto info = QuerySerialDeviceInfo(std::wstring(port.begin(), port.end()));
    std::string details;
    for (const auto* part : { &info.description, &info.manufacturer, &info.identifier })
    {
        if (part->empty()) continue;
        if (!details.empty()) details += ", ";
        details += ToUtf8(*part);
    }
    return details;
}
