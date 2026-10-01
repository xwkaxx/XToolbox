/**
 * @file serial_codec.cpp
 * @brief 不依赖 UI 或设备的字节解析、显示编码与发送后缀算法。
 * 显示转换允许替代无效字符；持久化转换必须使用可逆转义保留原始字节。
 */
#include "serial_codec.h"

#include <cstdint>

namespace SerialCodec
{
    // 支持连续 HEX 或按完整字节用空白分隔；拒绝半个字节、0x 前缀和其他符号。
    /// @brief 解析非空 HEX；失败会清空 bytes，text 必须是有效的 NUL 结尾字符串。
    bool ParseHex(const char* text, std::vector<unsigned char>& bytes, std::string& error)
    {
        bytes.clear();
        error.clear();
        // high 为 -1 表示等待高半字节；已有高半字节时遇到空白必须拒绝，不能跨空白拼字节。
        int high = -1;
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
        {
            const unsigned char c = *p;
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                if (high < 0) continue;
                error = "每个 HEX 字节需要两位，例如 01 A3 FF。";
                bytes.clear();
                return false;
            }
            const int value = c >= '0' && c <= '9' ? c - '0'
                : c >= 'a' && c <= 'f' ? c - 'a' + 10
                : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (value < 0)
            {
                error = "HEX 只能包含 0–9、A–F 和空白，不使用 0x 前缀。";
                bytes.clear();
                return false;
            }
            if (high < 0) high = value;
            else { bytes.push_back(static_cast<unsigned char>((high << 4) | value)); high = -1; }
        }
        if (high >= 0 || bytes.empty())
        {
            error = high >= 0 ? "HEX 位数必须为偶数。" : "请输入待发送的数据。";
            bytes.clear();
            return false;
        }
        return true;
    }

    // Abc 按 UTF-8 显示，控制字符用方框占位；原始字节仍保存在记录中。
    /// @brief 生成显示副本；HEX 包含尾随空格，字符模式不保证能反向还原原始字节。
    std::string FormatReceived(const std::vector<unsigned char>& bytes, bool hex)
    {
        static constexpr char digits[] = "0123456789ABCDEF";
        std::string text;
        text.reserve(bytes.size() * (hex ? 3 : 1));
        for (size_t i = 0; i < bytes.size();)
        {
            const unsigned char c = bytes[i];
            if (hex)
            {
                text += digits[c >> 4];
                text += digits[c & 15];
                text += ' ';
                ++i;
                continue;
            }
            if (c < 0x80)
            {
                // NUL 不产生可见字符，换行和制表符交给输出布局处理。
                if (c != 0)
                {
                    if ((c >= 0x20 && c <= 0x7E) || c == '\n' || c == '\r' || c == '\t')
                        text += static_cast<char>(c);
                    else
                        text += "\xE2\x96\xA1"; // U+25A1 WHITE SQUARE
                }
                ++i;
                continue;
            }

            // 仅接受 Unicode 合法的 UTF-8 首字节范围；后续检查排除过长编码、代理项和超出上限的码点。
            const size_t length = c >= 0xC2 && c <= 0xDF ? 2
                : c >= 0xE0 && c <= 0xEF ? 3
                : c >= 0xF0 && c <= 0xF4 ? 4 : 0;
            size_t consumed = 1;
            while (consumed < length && i + consumed < bytes.size())
            {
                const unsigned char next = bytes[i + consumed];
                if (next < 0x80 || next > 0xBF ||
                    (consumed == 1 && ((c == 0xE0 && next < 0xA0) ||
                        (c == 0xED && next > 0x9F) ||
                        (c == 0xF0 && next < 0x90) ||
                        (c == 0xF4 && next > 0x8F))))
                    break;
                ++consumed;
            }
            if (length != 0 && consumed == length)
                text.append(reinterpret_cast<const char*>(bytes.data() + i), length);
            else
                text += "\xEF\xBF\xBD"; // U+FFFD REPLACEMENT CHARACTER
            // 无效序列至少推进一个字节；显示使用替代字符，原始缓冲不被改写。
            i += consumed;
        }
        return text;
    }

    // 发送文本使用可逆转义，避免 NUL、控制字节和无效 UTF-8 在编辑框中丢失。
    /// @brief 解析持久化转义格式；失败时 bytes 可能含前缀，调用者必须检查返回值。
    bool ParseSendText(const char* input, std::vector<unsigned char>& bytes, std::string& error)
    {
        bytes.clear();
        const std::string text(input);
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] != '\\') { bytes.push_back(static_cast<unsigned char>(text[i])); continue; }
            if (++i == text.size()) { error = "反斜杠请写成 \\\\。"; return false; }
            const char c = text[i];
            if (c == '\\') bytes.push_back('\\');
            else if (c == 'n') bytes.push_back('\n');
            else if (c == 'r') bytes.push_back('\r');
            else if (c == 't') bytes.push_back('\t');
            else if (c == 'x' && i + 2 < text.size())
            {
                const auto digit = [](char value) { return value >= '0' && value <= '9' ? value - '0'
                    : value >= 'A' && value <= 'F' ? value - 'A' + 10
                    : value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1; };
                const int high = digit(text[i + 1]), low = digit(text[i + 2]);
                if (high < 0 || low < 0) { error = "字节转义需要两位 HEX，例如 \\x00。"; return false; }
                bytes.push_back(static_cast<unsigned char>((high << 4) | low));
                i += 2;
            }
            else { error = "支持的转义为 \\\\、\\n、\\r、\\t 和 \\xHH。"; return false; }
        }
        return true;
    }

    /// @brief 在 HEX 与可逆文本之间转换；失败时不得将输出提交到编辑器。
    /// @param output_capacity 目标缓冲总容量，包含末尾 NUL。
    bool ConvertSendText(const char* input, bool fromHex, std::string& output, std::string& error, std::size_t output_capacity)
    {
        output.clear();
        if (!*input) return true;
        std::vector<unsigned char> bytes;
        if (!(fromHex ? ParseHex(input, bytes, error) : ParseSendText(input, bytes, error))) return false;
        // 统一先解析为原始字节，避免在两种文本表示之间直接替换造成双重转义。
        if (!fromHex)
        {
            output = FormatReceived(bytes, true);
            if (!output.empty()) output.pop_back();
        }
        else
        {
            static constexpr char digits[] = "0123456789ABCDEF";
            for (unsigned char c : bytes)
            {
                if (c == '\\') output += "\\\\";
                else if (c == '\n') output += "\\n";
                else if (c == '\r') output += "\\r";
                else if (c == '\t') output += "\\t";
                else if (c >= 0x20 && c <= 0x7E) output += static_cast<char>(c);
                else { output += "\\x"; output += digits[c >> 4]; output += digits[c & 15]; }
            }
        }
        // 扩容只发生在临时字符串中；容量检查失败时由调用方保留原输入与模式。
        if (output.size() >= output_capacity)
        {
            error = "转换后内容超出发送框容量，已保留原内容和模式。";
            return false;
        }
        return true;
    }


    /// @brief 原地追加选定后缀；CRC 针对调用时已有的字节计算，调用方保证每个请求只追加一次。
    void AppendSendSuffix(std::vector<unsigned char>& bytes, int suffix)
    {
        switch (suffix)
        {
        case 1: bytes.push_back('\n'); break;
        case 2: bytes.push_back('\r'); break;
        case 3: bytes.push_back('\n'); bytes.push_back('\r'); break;
        case 4: bytes.push_back('\r'); bytes.push_back('\n'); break;
        case 5:
        {
            // CRC-16/MODBUS：初值 FFFF，反射多项式 A001，无最终异或。
            std::uint16_t crc = 0xFFFF;
            for (unsigned char byte : bytes)
            {
                crc ^= byte;
                for (int bit = 0; bit < 8; ++bit)
                    crc = static_cast<std::uint16_t>((crc >> 1) ^ ((crc & 1) ? 0xA001 : 0));
            }
            // Modbus RTU 校验按低字节、高字节追加，不能使用主机整数的内存布局直接复制。
            bytes.push_back(static_cast<unsigned char>(crc & 0xFF));
            bytes.push_back(static_cast<unsigned char>(crc >> 8));
            break;
        }
        default: break;
        }
    }
}
