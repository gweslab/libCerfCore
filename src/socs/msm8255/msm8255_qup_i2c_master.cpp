#include "msm8255_qup_i2c_master.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include "../i2c_slave.h"
#include "msm8255_i2c_slaves.h"
#include "msm8255_id.h"

namespace cerfcore {

REGISTER_SERVICE(Msm8255QupI2cMaster);

namespace {

constexpr uint32_t kTagShift     = 8u;
constexpr uint32_t kTagNop       = 0u;
constexpr uint32_t kTagStart     = 1u;
constexpr uint32_t kTagData      = 2u;
constexpr uint32_t kTagStop      = 3u;
constexpr uint32_t kTagRec       = 4u;
constexpr uint32_t kRecFullCount = 256u;
constexpr uint8_t  kAddrReadBit  = 1u;

}

bool Msm8255QupI2cMaster::ShouldRegister() {
    return emu_.Get<BoardContext>().GetSocId() == SocId::Msm8255;
}

bool Msm8255QupI2cMaster::Execute(uint16_t item, std::vector<uint8_t>& received) {
    const uint32_t tag  = item >> kTagShift;
    const uint8_t  byte = static_cast<uint8_t>(item);
    switch (tag) {
    case kTagNop:
        return true;
    case kTagStart:
        return Start(byte);
    case kTagData:
        return Transmit(byte);
    case kTagStop: {
        const bool acked = Transmit(byte);
        Release();
        return acked;
    }
    case kTagRec:
        Receive(byte != 0u ? byte : kRecFullCount, received);
        return true;
    }
    emu_.Get<Fatal>().Die("Msm8255QupI2cMaster: output item 0x%04X has no I2C tag", item);
}

bool Msm8255QupI2cMaster::Start(uint8_t addr_byte) {
    const uint8_t  addr  = static_cast<uint8_t>(addr_byte >> 1);
    auto&          buses = emu_.Get<Msm8255I2cSlaves>();
    const uint32_t index = buses.SlaveIndex(kMsm8255QupBus, addr);
    if (index == Msm8255I2cSlaves::kNoSlave) {
        if (!buses.OwnedBySlave(kMsm8255QupBus, addr)) {
            emu_.Get<Fatal>().Die("Msm8255QupI2cMaster: no modeled slave answers address 0x%02X",
                                  addr);
        }
        Release();
        return false;
    }
    if (open_ && index != slave_index_) {
        emu_.Get<Fatal>().Die("Msm8255QupI2cMaster: repeated start from slave 0x%02X to 0x%02X",
                              addr_, addr);
    }
    I2cSlave& slave = *buses.Slaves(kMsm8255QupBus)[index];
    if (!open_) slave.BusStart();
    slave.TxnStart(addr);
    open_        = true;
    reading_     = (addr_byte & kAddrReadBit) != 0u;
    addr_        = addr;
    slave_index_ = index;
    return true;
}

bool Msm8255QupI2cMaster::Transmit(uint8_t byte) {
    if (!open_ || reading_) {
        emu_.Get<Fatal>().Die("Msm8255QupI2cMaster: data byte 0x%02X with no write transfer open",
                              byte);
    }
    if (Slave().TxnWriteByte(static_cast<uint8_t>(addr_), byte)) return true;
    Release();
    return false;
}

void Msm8255QupI2cMaster::Receive(uint32_t count, std::vector<uint8_t>& received) {
    if (!open_ || !reading_) {
        emu_.Get<Fatal>().Die("Msm8255QupI2cMaster: receive of %u bytes with no read transfer open",
                              count);
    }
    I2cSlave& slave = Slave();
    for (uint32_t i = 0; i < count; ++i) {
        received.push_back(slave.TxnReadByte(static_cast<uint8_t>(addr_)));
    }
    Release();
}

void Msm8255QupI2cMaster::Release() {
    if (!open_) return;
    open_ = false;
    Slave().BusStop();
}

void Msm8255QupI2cMaster::Reset() {
    Release();
    reading_     = false;
    addr_        = 0u;
    slave_index_ = 0u;
}

I2cSlave& Msm8255QupI2cMaster::Slave() {
    return *emu_.Get<Msm8255I2cSlaves>().Slaves(kMsm8255QupBus)[slave_index_];
}

void Msm8255QupI2cMaster::SaveState(StateWriter& w) {
    w.Write<uint8_t>("qup_bus_open", open_ ? 1u : 0u);
    w.Write<uint8_t>("qup_reading", reading_ ? 1u : 0u);
    w.Write<uint32_t>("qup_addr", addr_);
    w.Write<uint32_t>("qup_slave_index", slave_index_);
    emu_.Get<Msm8255I2cSlaves>().SaveBusState(kMsm8255QupBus, w);
}

void Msm8255QupI2cMaster::RestoreState(StateReader& r) {
    uint8_t open = 0u, reading = 0u;
    r.Read("qup_bus_open", open);
    r.Read("qup_reading", reading);
    r.Read("qup_addr", addr_);
    r.Read("qup_slave_index", slave_index_);
    emu_.Get<Msm8255I2cSlaves>().RestoreBusState(kMsm8255QupBus, r);
    open_    = open != 0u;
    reading_ = reading != 0u;
}

void Msm8255QupI2cMaster::PostRestore() {
    emu_.Get<Msm8255I2cSlaves>().PostRestoreBus(kMsm8255QupBus);
}

}
