/**
 * @file serial_controller.h
 * @brief 主线程应用服务，隔离 ImGui 与通信实现。
 * 返回的模型引用均为借用，只在控制器生存期内有效；不得交给后台线程持有。
 */
#pragma once

#include "../communication/serial_session.h"
#include "../models/serial_send_editor.h"
#include "serial_settings.h"
#include "../communication/serial_devices.h"

/// @brief 界面中的未连接参数草稿；BuildConfig 将下标映射为驱动常量。
struct SerialConnectionOptions
{
    static constexpr DWORD BaudRates[] = { 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600 };
    static constexpr int CustomBaudIndex = 8;
    int baud_index = 4;
    DWORD custom_baud = 115200;
    int parity_index = 0;
    int data_bits_index = 3;
    int stop_bits_index = 0;
    /// @brief 检查选项下标与自定义波特率后提交 config；端口名称由打开流程验证，失败保持输出不变。
    bool BuildConfig(const std::string& port, SerialConfig& config) const;
};

/// @brief Accepted 仅表示请求已接受；NotConnected 用于连接引导，Rejected 表示内容或提交失败。
enum class SerialSendResult { Accepted, NotConnected, Rejected };

// 应用流程协调器，只由主线程访问；不依赖 ImGui。
class SerialController
{
public:
    SerialController() = default;
    explicit SerialController(std::unique_ptr<SerialTransport> transport);
    /// @brief 加载保存的偏好和上次提交，不自动打开设备。
    void InitializePreferences();
    /// @brief 发布记录开关并保存偏好；失败消息由 FlushMessages 进入输出区。
    void SavePreferences();
    // UI 编辑配置后调用 SavePreferences，同步记录选项并保存配置。
    SerialSettings& Preferences() { return preferences_; }
    SerialConnectionOptions& ConnectionOptions() { return connection_; }
    SerialSendEditor& Editor() { return editor_; }
    SerialHistory& History() { return history_; }
    SerialSessionSnapshot Status() const { return session_.Snapshot(); }

    /// @brief 用当前选项建立连接，失败信息保存在控制器消息缓冲中。
    bool Connect();
    /// @brief 关闭会话后归并最后一批收发结果。
    void Disconnect();
    /// @brief 结束工作线程，供外层资源释放或重新初始化前调用。
    void Shutdown();
    /// @brief 每帧在绘制前调用，消费会话增量并更新模型。
    void Pump();
    /// @brief 解析、追加后缀并提交发送请求，成功接受后记忆有效内容。
    SerialSendResult Send();
    void StopTimedSend() { session_.StopTimedSend(); }
    /// @brief 延迟恢复编辑文本，可重复调用。
    void InitializeEditor();
    /// @brief 转换成功后才保存模式；失败保留原草稿并记录原因。
    void ToggleSendMode();
    /// @brief 清除输入和记忆内容，不停止已经提交的连续发送。
    void ClearSendText();
    /// @brief 接收待处理结果后清原始缓存及计数，冻结现有显示文字。
    void ClearRawData();
    /// @brief 隐藏目前的文字，保留原始缓存和收发计数。
    void HideOutputText();
    /// @brief 将积累的应用提示作为历史错误记录追加。
    void FlushMessages();

    /// @brief 只查询当前可选端口，UI 自行决定缓存与刷新频率。
    SerialPortList RefreshPorts() const;
    /// @brief 取得当前所选端口的 UTF-8 设备摘要，缺失属性不输出占位文本。
    std::string PortDetails() const;

private:
    void ApplyRecordOptions();
    SerialSession session_;
    SerialSettings preferences_;
    SerialConnectionOptions connection_;
    SerialSendEditor editor_{ SerialSettings::SendBufferCapacity };
    SerialHistory history_;
    std::string messages_;
};
