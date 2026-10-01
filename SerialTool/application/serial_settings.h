/**
 * @file serial_settings.h
 * @brief 可持久化偏好及其加载、保存边界。
 * 通信参数草稿、线程状态、历史数据不属于此配置结构；错误说明使用 UTF-8。
 */
#pragma once

#include <cstddef>
#include <string>

// 只包含需要保存的配置，不依赖 ImGui，也不打开或操作串口。
struct SerialSettings
{
    // 容量按字节计且包含结尾 NUL；多字节字符的数量不等于可保存字节数。
    static constexpr std::size_t SendBufferCapacity = 2048;
    std::string selected_port;
    // 只保存最近被接受的提交；字符模式采用可逆转义，不直接保存占位符显示文本。
    std::string last_send_text;
    bool send_hex = true;
    bool timed_send = false;
    int send_interval_ms = 1000;
    // -1 表示无限，正数包含首次发送；0 和其他负数不属于合法配置。
    int send_repeat_count = -1;
    int send_append = 0; // 0:none, 1:LF, 2:CR, 3:LFCR, 4:CRLF, 5:CRC16/Modbus
    bool display_hex = true;
    bool show_timestamp = false;
    bool show_receive = true;
    bool show_send = true;
    float output_font_scale = 1.0f;
};

// 从 EXE 旁 config/preferences.json 加载；首次启动兼容旧 TXT 并创建 JSON。
// 缺少配置时使用默认值。返回 false 时 error 提供 UTF-8 错误说明。
bool LoadSerialSettings(SerialSettings& settings, std::string& error);
/// @brief 序列化后写入同目录临时文件并替换正式文件；调用方提供有效配置，失败通过 error 返回。
bool SaveSerialSettings(const SerialSettings& settings, std::string& error);
