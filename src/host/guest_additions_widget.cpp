#include "guest_additions_widget.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/host/customizations_editor.h>
#include <cerfcore/host/guest_resize_policy.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/host/task_manager_view.h>
#include <cerfcore/core/ui_text_service.h>

namespace cerfcore {

REGISTER_SERVICE(GuestAdditionsWidget);

bool GuestAdditionsWidget::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void GuestAdditionsWidget::OnReady() {
    emu_.Get<HostWidgetRegistry>().Register(this);
}

bool GuestAdditionsWidget::ResizeAvailable() const {
    const GuestResizePolicy* policy = emu_.TryGet<GuestResizePolicy>();
    return policy != nullptr && policy->Available();
}

bool GuestAdditionsWidget::ResizeEnabled() const {
    const GuestResizePolicy* policy = emu_.TryGet<GuestResizePolicy>();
    return policy != nullptr && policy->Enabled();
}

std::string GuestAdditionsWidget::DisplayName() const {
    return GetUiText(emu_, "common.guest_additions", "Guest Additions");
}

std::string GuestAdditionsWidget::Tooltip() const {
    if (!ResizeAvailable())
        return GetUiText(emu_, "guest_additions.guest_additions_click_for_tools", "Guest Additions - click for tools");
    return ResizeEnabled()
        ? GetUiText(emu_, "guest_additions.guest_additions_auto_resize_on_click_to_disable_right",
                    "Guest Additions - auto-resize ON (click to disable, right-click for tools)")
        : GetUiText(emu_, "guest_additions.guest_additions_auto_resize_off_click_to_enable_right",
                    "Guest Additions - auto-resize OFF (click to enable, right-click for tools)");
}

bool GuestAdditionsWidget::TryPrimaryAction() {
    if (!ResizeAvailable()) return false;
    auto& policy = emu_.Get<GuestResizePolicy>();
    policy.SetEnabled(!policy.Enabled());
    return true;
}

std::vector<WidgetControl> GuestAdditionsWidget::Controls() {
    GroupControl actions;

    if (emu_.TryGet<TaskManagerView>()) {
        ActionControl taskmgr;
        taskmgr.id    = "task_manager";
        taskmgr.label = GetUiText(emu_, "guest_additions.task_manager", "Task manager…");
        taskmgr.press = [this] { emu_.Get<TaskManagerView>().Show(); };
        actions.children.push_back(WidgetControl{std::move(taskmgr)});
    }

    if (emu_.TryGet<CustomizationsEditor>()) {
        ActionControl chres;
        chres.id    = "customizations";
        chres.label = GetUiText(emu_, "guest_additions.resolution_customizations", "Resolution / Customizations…");
        chres.press = [this] { emu_.Get<CustomizationsEditor>().Open(); };
        actions.children.push_back(WidgetControl{std::move(chres)});
    }

    if (ResizeAvailable()) {
        ToggleControl resize;
        resize.id    = "resize_to_window";
        resize.label = GetUiText(emu_, "guest_additions.resize_guest_to_window", "Resize guest to window");
        resize.value = ResizeEnabled();
        resize.set   = [this](bool on) { emu_.Get<GuestResizePolicy>().SetEnabled(on); };
        actions.children.push_back(WidgetControl{std::move(resize)});
    }

    std::vector<WidgetControl> controls{WidgetControl{InfoControl{
        GetUiText(emu_, "guest_additions.cerf_guest_additions", "CERF Guest Additions")}}};
    if (!actions.children.empty()) controls.push_back(WidgetControl{std::move(actions)});
    return controls;
}

WidgetIcon GuestAdditionsWidget::Icon() const {
    return ResourceIcon{ResizeEnabled() ? "ICON_GA" : "ICON_GA_DISABLED"};
}

bool GuestAdditionsWidget::PollDirty() {
    const bool on = ResizeEnabled();
    if (on == last_drawn_on_) return false;
    last_drawn_on_ = on;
    return true;
}

}
