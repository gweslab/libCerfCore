#include <cerfcore/host/frame_renderer.h>

namespace cerfcore {

void FrameRenderer::CaptureResetBaseline() {
    if (!latch_.BeginResetRelease()) return;
    HasFrame();
    latch_.EndResetRelease();
}

}
