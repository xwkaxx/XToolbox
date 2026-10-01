/**
 * @file serial_codec.h
 * @brief 无状态字节编解码与发送后缀处理。
 * 文本输入为 NUL 结尾字符串，字节数据使用显式长度容器；解析失败时调用方必须忽略结果。
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

// 纯字节转换，不读取界面状态、不访问设备或配置文件。
namespace SerialCodec
{
    // 连续或按完整字节用空白分隔的 HEX；不接受 0x 前缀、半字节和空输入。
    // text 必须指向有效的 NUL 结尾字符串；解析失败时 bytes 为空。
    bool ParseHex(const char* text, std::vector<unsigned char>& bytes, std::string& error);
    // HEX 或 UTF-8 显示文本；无效 UTF-8 替换显示，原始字节保持不变。
    std::string FormatReceived(const std::vector<unsigned char>& bytes, bool hex);
    // 解析配置使用的可逆转义文本，不是当前字符编辑框的输入解析器。
    bool ParseSendText(const char* input, std::vector<unsigned char>& bytes, std::string& error);
    // HEX 与可逆转义文本互转；容量包含结尾的 NUL。
    // fromHex 指示输入格式；字符模式的输出是可保存的转义文本，不是 UI 占位显示。
    bool ConvertSendText(const char* input, bool fromHex, std::string& output,
        std::string& error, std::size_t output_capacity);
    // 0:none, 1:LF, 2:CR, 3:LFCR, 4:CRLF, 5:CRC16/Modbus（低字节在前）。
    void AppendSendSuffix(std::vector<unsigned char>& bytes, int suffix);
}
