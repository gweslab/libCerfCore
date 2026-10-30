#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <mutex>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

class CompaqIpaqH3000SleeveNvm : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint8_t Read(const uint8_t* req, uint8_t len, uint8_t* reply);
    void    Write(const uint8_t* req, uint8_t len);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    std::mutex           mtx_;
    std::vector<uint8_t> nvm_;
};

}
