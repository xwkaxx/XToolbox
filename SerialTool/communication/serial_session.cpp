/**
 * @file serial_session.cpp
 * @brief 单串口会话的线程调度与事件交付。
 * 公开方法由主线程串行调用；只有 RunSerialIo 执行收发。队列与定时状态由 mutex 保护。
 */
#include "serial_session.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <system_error>
#include <stdexcept>
#include <utility>

namespace
{
    // 适配器只转发底层操作，不引入第二套句柄或线程所有权。
    class Win32SerialTransport final : public SerialTransport
    {
    public:
        bool Open(const SerialConfig& config) override { return port_.Open(config); }
        void Close() override { port_.Close(); }
        bool IsOpen() const override { return port_.IsOpen(); }
        bool Write(const void* data, std::size_t size, std::size_t& written) override
        { return port_.Write(data, size, written); }
        bool Read(void* data, std::size_t capacity, std::size_t& read) override
        { return port_.Read(data, capacity, read); }
        const std::wstring& Error() const override { return port_.GetLastErrorMessage(); }
    private:
        SerialPort port_;
    };
}

struct SerialSession::Impl
{
    static constexpr std::size_t ReceiveLimit = 256 * 1024;
    std::unique_ptr<SerialTransport> transport;
    std::atomic<bool> timestamp_enabled{ false };
    std::atomic<bool> show_receive{ true };
    std::atomic<bool> show_send{ true };
    std::thread thread;
    // stop/failed 跨线程无锁读取；它们不保护下方容器，容器仍必须持有 mutex。
    std::atomic<bool> stop{ true };
    std::atomic<bool> failed{ false };
    std::mutex mutex;
    std::deque<SerialSendRequest> outgoing_queue;
    bool timed_active = false; // 以下定时状态均由 mutex 保护。
    SerialSendRequest timed_request;
    int timed_remaining = 0; // -1 为连续发送。
    int timed_total = 0;
    std::uint64_t timed_completed = 0;
    int timed_interval_ms = 1000;
    std::uint64_t timed_generation = 0;
    std::chrono::steady_clock::time_point timed_next;
    std::deque<SerialDataRecord> records; // 同一队列保证工作线程观察到的收发顺序。
    // pending_bytes 仅计队列内原始字节；received/written 计实际 I/O，包括随后被裁剪的数据。
    std::size_t pending_bytes = 0;
    std::uint64_t received = 0;
    std::uint64_t written = 0;
    std::deque<std::wstring> notices;
    std::size_t dropped = 0;
    /// @brief 停止调度并等待工作线程退出；保留已产生的记录供 Poll 消费。
    void StopSerialIo()
    {
        stop = true;
        if (thread.joinable())
        {
            // 只取消本工作线程的同步 I/O；必须 join 后才允许关闭串口句柄。
            ::CancelSynchronousIo(thread.native_handle());
            thread.join();
        }
        // 不能持锁等待 join：工作线程退出前可能仍需持同一把锁提交最终记录。
        std::lock_guard<std::mutex> lock(mutex);
        outgoing_queue.clear();
        timed_active = false;
        timed_request.bytes.clear();
        ++timed_generation;
    }

    // 调用者持有工作队列的锁。显示缓冲可丢弃旧记录，实际收发计数独立保留。
    void QueueRecord(bool transmit, bool hex, const unsigned char* bytes, std::size_t size)
    {
        if (size == 0) return;
        if (transmit) written += size;
        else received += size;
        // 先累计真实字节数，再限制显示副本；否则缓存溢出会错误降低收发统计。
        if (size > ReceiveLimit)
        {
            dropped += size - ReceiveLimit;
            bytes += size - ReceiveLimit;
            size = ReceiveLimit;
        }
        // 字节数与条数同时限额，防止大量极小记录绕过字节容量约束。
        while (!records.empty()
            && (pending_bytes + size > ReceiveLimit || records.size() >= 2048))
        {
            pending_bytes -= records.front().bytes.size();
            dropped += records.front().bytes.size();
            records.pop_front();
        }
        SerialDataRecord record;
        // 在产生记录时冻结显示属性，之后切换开关不补显或重写既有历史。
        record.has_timestamp = timestamp_enabled.load();
        if (record.has_timestamp) ::GetLocalTime(&record.time);
        record.transmit = transmit;
        record.visible = transmit ? show_send.load() : show_receive.load();
        record.hex = hex;
        record.bytes.assign(bytes, bytes + size);
        pending_bytes += size;
        records.push_back(std::move(record));
    }

    /// @brief 顺序执行一个发送任务和一次读取；任何非主动停止的 I/O 失败结束循环。
    void RunSerialIo()
    {
        unsigned char buffer[4096];
        while (!stop)
        {
            SerialSendRequest outgoing;
            bool timed = false;
            std::uint64_t generation = 0;
            {
                std::lock_guard<std::mutex> lock(mutex);
                // 普通请求优先于到期的定时请求；取出副本后释放锁，慢速写入不能占用队列锁。
                if (!outgoing_queue.empty())
                {
                    outgoing = std::move(outgoing_queue.front());
                    outgoing_queue.pop_front();
                }
                else if (timed_active && std::chrono::steady_clock::now() >= timed_next)
                {
                    outgoing = timed_request;
                    timed = true;
                    generation = timed_generation;
                }
            }
            if (!outgoing.bytes.empty())
            {
                if (stop) break;
                std::size_t actualWritten = 0;
                const bool success = transport->Write(outgoing.bytes.data(), outgoing.bytes.size(), actualWritten);
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    // 部分写入只展示实际写出的字节，不把排队或失败当成发送成功。
                    QueueRecord(true, outgoing.hex, outgoing.bytes.data(), actualWritten);
                    // 停止或重启定时发送会递增代次；旧写入完成后不得扣减新任务次数或更新新任务进度。
                    if (timed && generation == timed_generation)
                    {
                        if (success) ++timed_completed;
                        if (!success || (timed_remaining > 0 && --timed_remaining == 0))
                        {
                            timed_active = false;
                            timed_request.bytes.clear();
                        }
                        // 采用单调时钟，并从写入完成后计时；不追赶错过的周期，避免突发补发。
                        timed_next = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds(timed_interval_ms);
                    }
                    if (!success)
                    {
                        if (notices.size() >= 128) notices.pop_front();
                        notices.push_back(L"[发送] " + transport->Error());
                    }
                }
                if (!success)
                {
                    if (!stop) failed = true;
                    break;
                }
            }
            if (stop) break;
            std::size_t count = 0;
            // 同步读取有驱动超时。循环串行执行收发，因此定时延时并非硬实时周期。
            if (!transport->Read(buffer, sizeof(buffer), count))
            {
                if (!stop)
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (notices.size() >= 128) notices.pop_front();
                    notices.push_back(L"[接收] " + transport->Error());
                    failed = true;
                }
                break;
            }
            if (count != 0)
            {
                std::lock_guard<std::mutex> lock(mutex);
                QueueRecord(false, true, buffer, count);
            }
        }
    }


    /// @brief 在持有 mutex 时校验提交条件；此检查与入队必须处于同一临界区。
    bool CanSubmit(const SerialSendRequest& request, std::wstring& error) const
    {
        if (!transport->IsOpen() || failed || stop)
            error = L"连接正在关闭，未提交数据。";
        else if (outgoing_queue.size() >= 64)
            error = L"等待发送的任务过多，请稍后重试。";
        else if (request.bytes.empty())
            error = L"请输入待发送的数据。";
        else
            return true;
        return false;
    }
};

/// @brief 创建使用 Windows 串口驱动的默认会话，构造期间不打开设备。
SerialSession::SerialSession() : SerialSession(std::make_unique<Win32SerialTransport>()) {}

/// @brief 接管传输对象的独占所有权；空指针作为编程错误抛出 invalid_argument。
SerialSession::SerialSession(std::unique_ptr<SerialTransport> transport)
    : impl_(std::make_unique<Impl>())
{
    if (!transport) throw std::invalid_argument("SerialSession requires a transport");
    impl_->transport = std::move(transport);
}

/// @brief 在释放 Impl 和传输对象前结束线程，避免后台访问已析构成员。
SerialSession::~SerialSession() { Close(); }

/// @brief 打开设备后启动工作线程；任一步失败均通过 error 返回原因。
bool SerialSession::Open(const SerialConfig& config, std::wstring& error)
{
    error.clear();
    auto& io = *impl_;
    if (io.transport->IsOpen())
    {
        error = L"串口已经打开，请先关闭。";
        return false;
    }
    if (!io.transport->Open(config))
    {
        error = io.transport->Error();
        return false;
    }
    // 只有设备打开成功才发布运行状态；此时尚未创建线程，初始化不会与 I/O 并发。
    io.stop = false;
    io.failed = false;
    try { io.thread = std::thread([worker = impl_.get()] { worker->RunSerialIo(); }); }
    // 设备已打开但线程创建失败时必须回滚，否则界面会留下无法工作的连接。
    catch (const std::system_error&)
    {
        io.stop = true;
        io.transport->Close();
        error = L"无法启动收发线程，串口已关闭。";
        return false;
    }
    return true;
}

/// @brief 可重复关闭；不丢弃关闭前已经产生的待消费事件。
void SerialSession::Close()
{
    impl_->StopSerialIo();
    impl_->transport->Close();
}

/// @brief 将请求加入普通发送队列；返回成功仅表示已接受，不表示已经写出。
bool SerialSession::SendOnce(SerialSendRequest request, std::wstring& error)
{
    error.clear();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->CanSubmit(request, error)) return false;
    impl_->outgoing_queue.push_back(std::move(request));
    return true;
}

/// @brief 以值语义保存本轮请求；首帧立即具备调度资格，后续按完成时间延时。
bool SerialSession::StartTimedSend(SerialSendRequest request, int interval_ms, int repeat_count,
    std::wstring& error)
{
    error.clear();
    auto& io = *impl_;
    std::lock_guard<std::mutex> lock(io.mutex);
    if (!io.CanSubmit(request, error)) return false;
    if (interval_ms < 1 || interval_ms > 86400000
        || (repeat_count != -1 && (repeat_count < 1 || repeat_count > 1000000)))
    {
        error = L"连续发送参数无效。";
        return false;
    }
    // 所有本轮参数在同一把锁下发布；输入框后续编辑不会改变这份字节快照。
    io.timed_request = std::move(request);
    io.timed_interval_ms = interval_ms;
    io.timed_remaining = repeat_count;
    io.timed_total = repeat_count;
    io.timed_completed = 0;
    io.timed_next = std::chrono::steady_clock::now();
    ++io.timed_generation;
    io.timed_active = true;
    return true;
}

/// @brief 禁止后续定时调度，不中断已取出的请求，也不关闭串口。
void SerialSession::StopTimedSend()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->timed_active = false;
    impl_->timed_request.bytes.clear();
    ++impl_->timed_generation;
}

/// @brief 发布后续记录的显示选项；三个原子开关独立更新，不是整体事务。
void SerialSession::SetRecordOptions(bool timestamp, bool receive, bool send)
{
    impl_->timestamp_enabled = timestamp;
    impl_->show_receive = receive;
    impl_->show_send = send;
}

/// @brief 获取连接与调度进度快照；调用方不能由此绕过提交时的再次校验。
SerialSessionSnapshot SerialSession::Snapshot() const
{
    auto& io = *impl_;
    std::lock_guard<std::mutex> lock(io.mutex);
    // 持有句柄不等于可以继续发送：失败已发生但尚未 Poll 关闭时，ready 必须为 false。
    const bool connected = io.transport->IsOpen();
    return { connected, connected && !io.failed && !io.stop,
        io.timed_active, io.timed_total, io.timed_completed };
}

/// @brief 消费一批事件；若工作线程报告失败，先完成关闭再交付最终结果。
SerialSessionEvents SerialSession::Poll()
{
    auto& io = *impl_;
    SerialSessionEvents events;
    // 失败通知只消费一次。Close 内部会取 mutex，此处必须在外层加锁之前调用。
    if (io.failed.exchange(false))
    {
        Close();
        events.disconnected_on_error = true;
    }
    std::lock_guard<std::mutex> lock(io.mutex);
    // 交换容器将后续历史合并和格式化移到锁外；同一批计数与记录一起交付。
    events.records.swap(io.records);
    events.notices.swap(io.notices);
    events.received = io.received;
    events.written = io.written;
    events.dropped = io.dropped;
    io.pending_bytes = io.received = io.written = io.dropped = 0;
    return events;
}
