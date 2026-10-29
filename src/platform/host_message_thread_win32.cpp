#define NOMINMAX
#include "host_message_thread.h"

#include <cerfcore/core/instance_threads.h>

namespace cerfcore::host_platform {

HostMessageThread::~HostMessageThread() {
    if (ready_event_) CloseHandle(ready_event_);
}

void HostMessageThread::Start(InstanceThreads& threads, PrepareHandler prepare, StartHandler on_start,
                              SignalHandler on_signal, MessageHandler on_message) {
    shutdown_.store(false, std::memory_order_release);
    if (ready_event_) ResetEvent(ready_event_);
    else              ready_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = threads.Start([this, prepare, on_start, on_signal, on_message] { Run(prepare, on_start, on_signal, on_message); },
                            [this] { SetEvent(ready_event_); });
    WaitForSingleObject(ready_event_, INFINITE);
}

void HostMessageThread::Stop() {
    shutdown_.store(true, std::memory_order_release);
    if (thread_id_) PostThreadMessageW(thread_id_, WM_QUIT, 0, 0);
    if (thread_.joinable()) thread_.join();
    thread_id_ = 0;
}

void HostMessageThread::Post(UINT message, WPARAM wparam, LPARAM lparam) const {
    if (thread_id_) PostThreadMessageW(thread_id_, message, wparam, lparam);
}

void HostMessageThread::Run(const PrepareHandler& prepare, const StartHandler& on_start,
                            const SignalHandler& on_signal, const MessageHandler& on_message) {
    thread_id_         = GetCurrentThreadId();
    const HANDLE wait  = prepare ? prepare() : nullptr;
    const DWORD  waits = wait ? 1u : 0u;

    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    SetEvent(ready_event_);

    if (on_start) on_start();

    while (!shutdown_.load(std::memory_order_acquire)) {
        const DWORD w = MsgWaitForMultipleObjectsEx(waits, waits ? &wait : nullptr, INFINITE, QS_ALLINPUT,
                                                    MWMO_INPUTAVAILABLE);
        if (waits && w == WAIT_OBJECT_0) {
            on_signal();
            continue;
        }
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return;
            on_message(msg);
        }
    }
}

}
