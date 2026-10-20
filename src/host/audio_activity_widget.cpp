#include "audio_activity_widget.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/core/ui_text_service.h>

namespace cerfcore {

std::string AudioActivityWidget::DisplayName() const {
    return GetUiText(emu_, "common.audio", "Audio");
}

std::string AudioActivityWidget::Tooltip() const {
    return GetUiText(emu_, "audio.activity_hint", "Audio - TX: sound output, RX: microphone");
}

void AudioActivityWidget::NotePresent() {
    if (registered_) return;
    registered_ = true;
    emu_.Get<HostWidgetRegistry>().Register(this);
}

REGISTER_SERVICE(AudioActivityWidget);

}
