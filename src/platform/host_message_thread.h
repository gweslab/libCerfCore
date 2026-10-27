#pragma once

#include <windows.h>

#include <atomic>
#include <functional>
#include <thread>

namespace cerfcore::host_platform {

class HostMessageThread {
public:
    using PrepareHandler = std::function<HANDLE()>;
    using StartHandler   = std::function<void()>;
    using SignalHandler  = std::function<void()>;
    using MessageHandler = std::function<void(const MSG&)>;

    HostMessageThread() = default;
    ~HostMessageThread();
    HostMessageThread(const HostMessageThread&)            = delete;
    HostMessageThread& operator=(const HostMessageThread&) = delete;

    void Start(PrepareHandler prepare, StartHandler on_start, SignalHandler on_signal, MessageHandler on_message);
    void Stop();
    void Post(UINT message, WPARAM wparam = 0, LPARAM lparam = 0) const;
    DWORD ThreadId() const { return thread_id_; }

private:
    void Run(const PrepareHandler& prepare, const StartHandler& on_start, const SignalHandler& on_signal,
             const MessageHandler& on_message);

    DWORD             thread_id_   = 0;
    HANDLE            ready_event_ = nullptr;
    std::thread       thread_;
    std::atomic<bool> shutdown_{false};
};

}
