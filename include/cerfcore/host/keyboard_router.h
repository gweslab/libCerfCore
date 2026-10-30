#pragma once

#include "../core/service.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace cerfcore {

class KeyboardInput;

class KeyboardRouter : public Service {
public:
    using Service::Service;

    void Register(KeyboardInput* src);
    void OnHostKey(uint8_t vk, bool key_up);
    void OnStockKey(uint8_t vk, bool key_up);
    bool Held(uint8_t vk) const;

    std::vector<KeyboardInput*> Sources();
    KeyboardInput*              Active();
    void                        SetActive(KeyboardInput* src);
    void                        RestoreActiveByName(const std::string& name);

    void ReevaluateAuto();
    void RearmAutoSelect();

    bool UserPicked() const;
    void RestoreUserPicked(bool picked);

private:
    void           SelectAutoLocked();
    KeyboardInput* StockLocked() const;
    void           Route(uint8_t vk, bool key_up, bool stock);

    mutable std::mutex               mtx_;
    std::vector<KeyboardInput*>      sources_;
    KeyboardInput*                   active_ = nullptr;
    bool                             user_picked_ = false;
    std::array<KeyboardInput*, 256>  key_owner_{};
};

}
