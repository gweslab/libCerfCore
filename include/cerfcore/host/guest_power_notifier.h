#pragma once

#include "../core/service.h"
#include "../core/callback_list.h"

#include <functional>

namespace cerfcore {

enum class ResumeSource { User, Hardware };

class GuestPowerNotifier : public Service {
public:
    using Service::Service;

    void NotifyPowerDown();
    void NotifyReboot();
    void NotifyResetReleased();
    void NotifyResume(ResumeSource src);
    void NotifyHardReset();

    void RegisterRebootListener(std::function<void()> fn);

private:
    void Banner(const char* line);

    CallbackList<> reboot_listeners_;
};

}
