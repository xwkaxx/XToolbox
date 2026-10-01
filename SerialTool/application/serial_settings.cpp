/**
 * @file serial_settings.cpp
 * @brief 偏好配置的文件格式、路径解析和兼容迁移。
 * 仅由主线程调用；保存不持有串口队列锁，读取不触发连接或发送。
 */
#include "serial_settings.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>
#include "../../ThirdParty/nlohmann/json.hpp"

namespace
{
    /// @brief 基于当前 EXE 位置解析配置路径；无法取得完整路径时返回空路径。
    std::filesystem::path PreferencesPath()
    {
        wchar_t executable[32768];
        const DWORD size = ::GetModuleFileNameW(nullptr, executable, 32768);
        if (size == 0 || size >= 32768) return {};
        // 以 EXE 所在目录为准，不受快捷方式、启动器的工作目录影响。
        return std::filesystem::path(executable).parent_path() / L"config" / L"preferences.json";
    }

    // JSON 库负责中文、引号和换行的转义；先完成序列化，再替换配置文件。
    /// @brief 序列化后写临时文件再替换正式配置；失败保留错误状态交由外层报告。
    bool SavePreferences(const std::filesystem::path& path, const SerialSettings& config)
    {
        if (path.empty()) return false;
        std::string content;
        try
        {
            // 保留稳定字段顺序方便人工查看；version 约束当前可解析的配置格式。
            const nlohmann::ordered_json settings = {
                { "version", 1 },
                { "serial", { { "port", config.selected_port } } },
                { "send", { { "mode", config.send_hex ? "hex" : "text" },
                            { "content", config.last_send_text },
                            { "append", config.send_append },
                            { "timed", config.timed_send },
                            { "interval_ms", config.send_interval_ms },
                            { "repeat_count", config.send_repeat_count } } },
                { "view", {
                    { "mode", config.display_hex ? "hex" : "text" },
                    { "timestamp", config.show_timestamp },
                    { "show_receive", config.show_receive },
                    { "show_send", config.show_send },
                    { "font_scale", config.output_font_scale }
                } }
            };
            content = settings.dump(2) + "\n";
        }
        catch (const nlohmann::json::exception&) { return false; }
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return false;
        // 临时文件与正式文件位于同一目录；写入和关闭成功后才进入替换步骤。
        auto temporary = path;
        temporary += L".tmp";
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file << content;
        file.close();
        if (!file) return false;
        // 写完临时文件后替换，避免中途退出破坏上一份配置。
        return ::MoveFileExW(temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }

    /// @brief 尽力读取旧 TXT 格式；格式不匹配或内容过大时保留传入默认值。
    void LoadLegacyPreferences(const std::filesystem::path& path, SerialSettings& config)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size > 8192) return; // 首次启动没有配置；异常文件不影响程序启动。
        std::ifstream file(path, std::ios::binary);
        std::string version, port, hex, encoded;
        if (!std::getline(file, version) || !std::getline(file, port)
            || !std::getline(file, hex) || !std::getline(file, encoded)) return;
        for (std::string* line : { &version, &port, &hex, &encoded })
            if (!line->empty() && line->back() == '\r') line->pop_back();
        if (version != "SerialToolPreferences1" || (hex != "0" && hex != "1")) return;
        if (!port.empty() && (port.size() <= 3 || port.size() > 32
            || port.compare(0, 3, "COM") != 0
            || !std::all_of(port.begin() + 3, port.end(),
                [](char c) { return c >= '0' && c <= '9'; }))) return;
        // 旧文本内容使用 HEX 编码；长度约束预留编辑框末尾 NUL，解码时还要拒绝内嵌 NUL。
        if (encoded.size() % 2 || encoded.size() / 2 >= SerialSettings::SendBufferCapacity) return;
        const auto digit = [](char c) -> int
        {
            return c >= '0' && c <= '9' ? c - '0'
                : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        };
        std::string text;
        for (std::size_t i = 0; i < encoded.size(); i += 2)
        {
            const int high = digit(encoded[i]), low = digit(encoded[i + 1]);
            if (high < 0 || low < 0 || (high == 0 && low == 0)) return;
            text += static_cast<char>((high << 4) | low);
        }
        // 完整校验后再恢复，不连接端口、不提交发送任务。
        config.selected_port = port;
        config.send_hex = hex == "1";
        config.last_send_text = text;
    }

    /// @brief 读取并验证 JSON 字段，验证完成后才向配置对象提交结果。
    bool LoadPreferences(const std::filesystem::path& path, SerialSettings& config)
    {
        try
        {
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error || size > 32768) return false;
            std::ifstream file(path, std::ios::binary);
            if (!file) return false;
            // 解析异常和字段类型异常统一转换为 false，不让损坏的本地配置中断启动。
            const auto settings = nlohmann::json::parse(file);
            if (!settings.is_object() || !settings.contains("version")
                || !settings["version"].is_number_integer() || settings["version"] != 1)
                return false;
            const auto& serial = settings.at("serial");
            const auto& send = settings.at("send");
            if (!serial.is_object() || !send.is_object()) return false;
            const std::string port = serial.at("port").get<std::string>();
            const std::string mode = send.at("mode").get<std::string>();
            const std::string text = send.at("content").get<std::string>();
            if (mode != "hex" && mode != "text") return false;
            if (send.contains("append") && !send["append"].is_number_integer()) return false;
            const auto append = send.value("append", std::int64_t(0));
            if (append < 0 || append > 5) return false;
            const bool timed = send.value("timed", false);
            if (send.contains("interval_ms") && !send["interval_ms"].is_number_integer()) return false;
            if (send.contains("repeat_count") && !send["repeat_count"].is_number_integer()) return false;
            // 先用宽整数接收再检查范围，避免直接转换为 int 时截断异常配置值。
            const auto interval = send.value("interval_ms", std::int64_t(1000));
            const auto repeats = send.value("repeat_count", std::int64_t(-1));
            if (interval < 1 || interval > 86400000) return false;
            if (repeats != -1 && (repeats < 1 || repeats > 1000000)) return false;
            if (!port.empty() && (port.size() <= 3 || port.size() > 32
                || port.compare(0, 3, "COM") != 0
                || !std::all_of(port.begin() + 3, port.end(),
                    [](char c) { return c >= '0' && c <= '9'; }))) return false;
            if (text.size() >= SerialSettings::SendBufferCapacity || text.find('\0') != std::string::npos)
                return false;
            // 旧版 JSON 没有 view 时使用默认值，保持配置兼容。
            const auto view = settings.value("view", nlohmann::json::object());
            if (!view.is_object()) return false;
            const std::string displayMode = view.value("mode", std::string("hex"));
            if (displayMode != "hex" && displayMode != "text") return false;
            const bool timestamp = view.value("timestamp", false);
            const bool showReceive = view.value("show_receive", true);
            const bool showSend = view.value("show_send", true);
            const float fontScale = view.value("font_scale", 1.0f);
            if (!std::isfinite(fontScale) || fontScale < 0.6f || fontScale > 2.0f) return false;

            // 所有字段验证成功后一起恢复，错误配置不会造成半恢复状态。
            config.display_hex = displayMode == "hex";
            config.show_timestamp = timestamp;
            config.show_receive = showReceive;
            config.show_send = showSend;
            config.output_font_scale = fontScale;
            config.selected_port = port;
            config.send_hex = mode == "hex";
            config.send_append = static_cast<int>(append);
            config.timed_send = timed;
            config.send_interval_ms = static_cast<int>(interval);
            config.send_repeat_count = static_cast<int>(repeats);
            config.last_send_text = text;
            return true;
        }
        catch (const nlohmann::json::exception&) { return false; }
    }

}

/// @brief 对外保存入口；清除旧错误并以 UTF-8 返回本次失败原因。
bool SaveSerialSettings(const SerialSettings& settings, std::string& error)
{
    error.clear();
    if (!SavePreferences(PreferencesPath(), settings))
    {
        error = "无法保存配置，下次启动可能无法恢复本次选择。";
        return false;
    }
    return true;
}

/// @brief 先恢复默认值，再加载 JSON 或迁移旧配置；返回值同时反映首次保存是否成功。
bool LoadSerialSettings(SerialSettings& settings, std::string& error)
{
    settings = SerialSettings{};
    error.clear();
    const auto path = PreferencesPath();
    std::error_code fileError;
    // 区分不存在与路径不可访问：仅不存在时允许迁移并创建，访问错误不能静默覆盖。
    const bool exists = !path.empty() && std::filesystem::exists(path, fileError);
    if (path.empty() || fileError)
    {
        error = "无法访问配置文件。";
        return false;
    }
    // 已有 JSON 是唯一权威来源；即使内容无效，也不使用陈旧 TXT 偷换用户配置。
    if (exists)
    {
        if (!LoadPreferences(path, settings))
        {
            error = "preferences.json 内容无效或无法读取，未恢复配置。";
            return false;
        }
        return true; // 已有 JSON 时，不使用旧 TXT 覆盖。
    }
    auto legacy = path;
    legacy.replace_extension(L".txt");
    if (std::filesystem::exists(legacy, fileError))
        LoadLegacyPreferences(legacy, settings);
    else
    {
        wchar_t directory[32768];
        const DWORD size = ::GetEnvironmentVariableW(L"LOCALAPPDATA", directory, 32768);
        if (size > 0 && size < 32768)
            LoadLegacyPreferences(std::filesystem::path(directory) / L"XToolbox"
                / L"SerialTool" / L"preferences.txt", settings);
    }
    // 旧文件不删除；创建 JSON 失败时仍保留已经读取到的配置。
    return SaveSerialSettings(settings, error);
}
