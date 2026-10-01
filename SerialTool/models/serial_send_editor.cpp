/**
 * @file serial_send_editor.cpp
 * @brief 主线程发送编辑模型，维护可见字符与原始字节的映射。
 * 输入控件可以修改 Buffer，但字符模式编辑后必须 SyncText；转换失败不提交模式变化。
 */
#include "serial_send_editor.h"
#include "serial_codec.h"

#include <algorithm>
#include <cstring>
#include <utility>

/// @brief 分配固定编辑容量，至少保留一个 NUL 位置，不在后续编辑中移动缓冲。
SerialSendEditor::SerialSendEditor(std::size_t capacity)
    : buffer_((std::max)(std::size_t(1), capacity), '\0')
{
}

/// @brief 接受已保存文本及模式并重置映射；输入无效时整个对象保持原状。
bool SerialSendEditor::Restore(const std::string& saved_text, bool hex)
{
    // 写入前检查长度与内嵌 NUL，防止 C 字符串视图被截断后与记忆内容不一致。
    if (saved_text.size() >= Capacity() || saved_text.find('\0') != std::string::npos)
        return false;
    std::memcpy(Buffer(), saved_text.c_str(), saved_text.size() + 1);
    last_submitted_ = saved_text;
    hex_ = hex;
    tokens_.clear();
    initialized_ = false;
    return true;
}

/// @brief 首次显示时还原字符模式内容；已尝试恢复后不在每帧重复解析或重复提示。
bool SerialSendEditor::Initialize(std::string& error)
{
    error.clear();
    if (initialized_) return true;
    // 一次恢复失败只报告一次；若重新加载配置，Restore 会重置此标记。
    initialized_ = true;
    if (hex_) return true;
    std::vector<unsigned char> bytes;
    // 兼容旧配置的转义文本，编辑框本身不再解析转义。
    if (!SerialCodec::ParseSendText(Buffer(), bytes, error))
    {
        const auto* begin = reinterpret_cast<const unsigned char*>(Buffer());
        bytes.assign(begin, begin + std::strlen(Buffer()));
    }
    error.clear();
    if (!SetTextBytes(bytes))
    {
        error = "已保存内容超出字符显示容量。";
        return false;
    }
    return true;
}

/// @brief 按 token 顺序拼接原始字节；不可从占位显示字符反推。
std::vector<unsigned char> SerialSendEditor::TextBytes() const
{
    std::vector<unsigned char> bytes;
    for (const auto& token : tokens_)
        bytes.insert(bytes.end(), token.bytes.begin(), token.bytes.end());
    return bytes;
}

/// @brief 构造字符映射和显示缓冲，容量足够后才一次性提交。
bool SerialSendEditor::SetTextBytes(const std::vector<unsigned char>& bytes)
{
    // 先在局部容器构建所有 token，避免容量超限时破坏现有映射。
    std::vector<TextToken> tokens;
    std::string display;
    for (size_t i = 0; i < bytes.size();)
    {
        const unsigned char c = bytes[i];
        size_t length = c >= 0xC2 && c <= 0xDF ? 2
            : c >= 0xE0 && c <= 0xEF ? 3 : c >= 0xF0 && c <= 0xF4 ? 4 : 1;
        if (i + length > bytes.size()) length = 1;
        for (size_t j = 1; j < length; ++j)
            if (bytes[i + j] < 0x80 || bytes[i + j] > 0xBF) { length = 1; break; }
        TextToken token;
        // 即使对应文本显示为方框或替代符，也保留来源字节供未编辑部分无损发送。
        token.bytes.assign(bytes.begin() + i, bytes.begin() + i + length);
        token.text = SerialCodec::FormatReceived(token.bytes, false);
        // NUL 和 CR 用方框占位，编辑时仍能逐字节删除。
        if (token.text.empty() || c == '\r') token.text = "\xE2\x96\xA1";
        display += token.text;
        tokens.push_back(std::move(token));
        i += length;
    }
    if (display.size() >= Capacity()) return false;
    tokens_ = std::move(tokens);
    std::memcpy(Buffer(), display.c_str(), display.size() + 1);
    return true;
}

// 未编辑的字符保留原始字节，新增字符按 UTF-8 编码。
/// @brief 保留未改动前后缀的原始字节，只把中间编辑区域解释为新 UTF-8 文本。
void SerialSendEditor::SyncText()
{
    if (hex_) return;
    const std::string edited(Buffer());
    size_t first = 0, prefix = 0, last = tokens_.size(), suffix = edited.size();
    // 扫描相同前缀时按完整 token 推进，不能按单个 UTF-8 字节切断多字节字符。
    while (first < last)
    {
        const auto& part = tokens_[first].text;
        if (edited.compare(prefix, part.size(), part) != 0) break;
        prefix += part.size();
        ++first;
    }
    // 后缀扫描不得越过前缀；suffix - prefix 是尚未匹配的编辑区长度。
    while (last > first)
    {
        const auto& part = tokens_[last - 1].text;
        if (part.size() > suffix - prefix ||
            edited.compare(suffix - part.size(), part.size(), part) != 0) break;
        suffix -= part.size();
        --last;
    }
    std::vector<TextToken> tokens(tokens_.begin(), tokens_.begin() + first);
    // 只有新增或替换的中间文本生成新字节；旧占位符的来源字节由两端 token 保留。
    for (size_t i = prefix; i < suffix;)
    {
        size_t end = i + 1;
        while (end < suffix && (static_cast<unsigned char>(edited[end]) & 0xC0) == 0x80) ++end;
        TextToken token;
        token.text = edited.substr(i, end - i);
        token.bytes.assign(edited.begin() + i, edited.begin() + end);
        tokens.push_back(std::move(token));
        i = end;
    }
    tokens.insert(tokens.end(), tokens_.begin() + last, tokens_.end());
    tokens_ = std::move(tokens);
}


/// @brief 同时转换当前草稿与记忆内容，全部准备成功后更新模式。
bool SerialSendEditor::ToggleMode(std::string& error)
{
    error.clear();
    std::string converted, remembered;
    std::vector<unsigned char> bytes;
    if (hex_ && Buffer()[0])
    {
        if (!SerialCodec::ParseHex(Buffer(), bytes, error)) return false;
    }
    else if (!hex_)
        bytes = TextBytes();
    // 先转换记忆内容，防止草稿已切换模式但持久化内容仍处于旧模式。
    if (!SerialCodec::ConvertSendText(last_submitted_.c_str(), hex_, remembered, error, Capacity()))
        return false;
    if (hex_)
    {
        if (!SetTextBytes(bytes))
        {
            error = "转换后内容超出发送框容量。";
            return false;
        }
    }
    else
    {
        converted = SerialCodec::FormatReceived(bytes, true);
        if (!converted.empty()) converted.pop_back();
        if (converted.size() >= Capacity())
        {
            error = "转换后内容超出发送框容量。";
            return false;
        }
        std::memcpy(Buffer(), converted.c_str(), converted.size() + 1);
    }
    // 所有可能失败的转换已完成，此处才提交模式和记忆内容。
    last_submitted_ = std::move(remembered);
    hex_ = !hex_;
    return true;
}

/// @brief 清除编辑内容、字节映射和有效提交记忆，保持当前显示模式。
void SerialSendEditor::Clear()
{
    Buffer()[0] = '\0';
    tokens_.clear();
    last_submitted_.clear();
}

/// @brief 获取待发送的非空原始字节；返回 false 时不可提交输出内容。
bool SerialSendEditor::ReadBytes(std::vector<unsigned char>& bytes, std::string& error) const
{
    error.clear();
    if (hex_) return SerialCodec::ParseHex(Buffer(), bytes, error);
    bytes = TextBytes();
    if (bytes.empty())
    {
        error = "请输入待发送的数据。";
        return false;
    }
    return true;
}

/// @brief 为当前模式生成可恢复的配置文本，不修改最后一次有效提交。
bool SerialSendEditor::MakeSavedText(const std::vector<unsigned char>& bytes,
    std::string& saved_text, std::string& error) const
{
    error.clear();
    saved_text = Buffer();
    if (hex_) return true;
    const std::string rawHex = SerialCodec::FormatReceived(bytes, true);
    return SerialCodec::ConvertSendText(rawHex.c_str(), true, saved_text, error, Capacity());
}

/// @brief 接收控制器确认已被会话接受的内容；不表示底层写入成功。
void SerialSendEditor::RememberSubmitted(std::string saved_text)
{
    last_submitted_ = std::move(saved_text);
}
