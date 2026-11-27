#pragma once
#include <cerfcore/core/service.h>
#include <cstdint>

namespace cerfcore {

class EmulatedMemory;
class StateWriter;
class StateReader;

class EmulatedMemoryImage : public Service {
public:
    using Service::Service;
    void OnReady() override;

    void WipeVolatileRegions();

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

    void SaveFlashRegions(StateWriter& w);
    void RestoreFlashRegions(StateReader& r);

    void RestoreRegions(StateReader& r, bool flash, bool write);
    void RestoreSections(StateReader& r, bool write, bool with_flash);

private:
    uint32_t RegionCount(bool flash) const;
    void     SaveRegions(StateWriter& w, bool flash);

    EmulatedMemory* memory_ = nullptr;
};

}
