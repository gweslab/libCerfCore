#pragma once

#include "../core/service.h"
#include "state_image_format.h"
#include "../core/callback_list.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace cerfcore {

class StateWriter;
class StateReader;

/* Callers must run Save/Restore on a thread other than the JIT thread -
   JitRunner::Pause self-deadlocks if called from the JIT thread. */
class Hibernation : public Service {
public:
    using Service::Service;
    ~Hibernation() override;

    std::string DefaultStatePath() const;
    bool        DefaultStateExists() const;

    bool Save(const std::string& path);
    bool Restore(const std::string& path, bool ram_only = false);

    void SaveAsync(const std::string& path, std::function<void(bool)> on_done = {});
    void RestoreAsync(const std::string& path, bool ram_only = false);

    void RegisterPreSaveListener(std::function<void()> fn);
    void RegisterPostRestoreListener(std::function<void()> fn);
    void RegisterRomImage(uint64_t bytes);

    void OnShutdown() override;

private:
    enum class RestoreOutcome { Refused, Resumed, MemoryOnly, MemoryAfterRefusal };

    struct ImageMatch {
        bool same_rom;
        bool same_peripherals;
    };

    std::string      DeviceDirFile(const char* name) const;
    StateImageHeader LiveHeader() const;
    bool             WriteImage(const std::string& path, std::string& error);
    void             SaveSection(StateWriter& w, StateSection section);
    void             RestoreSection(StateReader& r, StateSection section);
    ImageMatch       ReadHeader(StateReader& r);
    void             ApplyImage(StateReader& r);
    void             ApplyMemory(const std::string& path, bool write, bool with_flash);
    void             FillMemory(const std::string& path, bool with_flash);
    bool             ApplyOrRollBack(const std::string& rollback_path,
                                     const std::function<void()>& apply, std::string& refusal);
    RestoreOutcome   ApplyStateFile(const std::string& path, bool ram_only, std::string& reason);
    void             FinishRestore(RestoreOutcome outcome, const std::string& reason);
    void             RollBack(const std::string& rollback_path);
    void             RestorePeripherals(StateReader& r);
    void             RestorePresentation(StateReader& r);
    uint32_t         PeripheralLayoutSig() const;
    void     Progress(const char* fmt, ...);
    void     JoinWorker();
    bool     ClaimWorker();

    std::thread       worker_;
    std::atomic<bool> busy_{false};

    CallbackList<>                     pre_save_listeners_;
    CallbackList<>                     post_restore_listeners_;
    uint64_t                           registered_rom_bytes_ = 0;
};

}
