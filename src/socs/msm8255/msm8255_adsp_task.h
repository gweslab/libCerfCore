#pragma once

#include <cstdint>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

struct Msm8255AdspCommand {
    uint32_t              queue = 0;
    std::vector<uint32_t> words;
};

class Msm8255AdspTask {
public:
    virtual ~Msm8255AdspTask() = default;

    virtual uint32_t Module() const = 0;
    virtual void Command(const Msm8255AdspCommand& command) = 0;
    virtual void Reset() {}

    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}
    virtual void PostRestore() {}
};

}
