/**
 * @file serial_send_editor.h
 * @brief 字符缓冲与原始发送字节之间的编辑模型。
 * 占位显示不等于实际字节；调用顺序为 Restore/Initialize、编辑/SyncText、ReadBytes/提交。
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

// 主线程使用的发送编辑模型；不访问 ImGui、串口或配置文件。
class SerialSendEditor
{
public:
    /// @brief 固定分配至少一个字节的缓冲；capacity 包含结尾 NUL。
    explicit SerialSendEditor(std::size_t capacity);

    // 配置中的字符模式内容使用可逆转义；首次显示时还原为编辑文本。
    bool Restore(const std::string& saved_text, bool hex);
    /// @brief 首次把配置转义文本转换为编辑 token；重复调用不重复恢复。
    bool Initialize(std::string& error);
    bool IsHex() const { return hex_; }
    // 供输入控件原地编辑；必须保留 NUL 结尾，字符模式编辑后调用 SyncText。
    char* Buffer() { return buffer_.data(); }
    const char* Buffer() const { return buffer_.data(); }
    std::size_t Capacity() const { return buffer_.size(); }
    /// @brief 同步字符模式的修改，保留相同前后缀 token 携带的原始字节。
    void SyncText();
    /// @brief 同时转换草稿与提交记忆；失败不提交新的模式和记忆内容。
    bool ToggleMode(std::string& error);
    /// @brief 清空当前草稿、token 和提交记忆，保持当前模式。
    void Clear();

    /// @brief 取得待发送字节，字符模式调用前应已 SyncText；空内容视为错误。
    bool ReadBytes(std::vector<unsigned char>& bytes, std::string& error) const;
    /// @brief 从未追加后缀的字节生成可持久化内容；不改变编辑缓冲。
    bool MakeSavedText(const std::vector<unsigned char>& bytes,
        std::string& saved_text, std::string& error) const;
    // 仅在发送请求被接受后调用；草稿与无效输入不能覆盖最后一次有效提交。
    void RememberSubmitted(std::string saved_text);
    const std::string& LastSubmittedText() const { return last_submitted_; }

private:
    // 一个显示单元关联其来源字节；text 可是方框，bytes 仍保留控制字符或非法 UTF-8。
    struct TextToken
    {
        std::string text;
        std::vector<unsigned char> bytes;
    };
    std::vector<unsigned char> TextBytes() const;
    bool SetTextBytes(const std::vector<unsigned char>& bytes);

    // 对象生存期内缓冲不扩容，供 ImGui 原地写入；模式切换仍需维护容量与 NUL 约束。
    std::vector<char> buffer_;
    std::vector<TextToken> tokens_;
    std::string last_submitted_;
    bool hex_ = true;
    bool initialized_ = false;
};
