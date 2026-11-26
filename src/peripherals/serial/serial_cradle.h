#pragma once

#include <cerfcore/host/machine_socket.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cerfcore {

class CerfEmulator;
class SerialLine;
class SerialEndpoint;
class StateWriter;
class StateReader;

class SerialCradle : public MachineSocket {
public:
    SerialCradle(CerfEmulator& emu, SerialLine& line, ObjectIdentity identity);
    ~SerialCradle();

    void OnShutdown();

    void SaveCradleState(StateWriter& w);
    void RestoreCradleState(StateReader& r);

    std::string       ObjectId() const override { return identity_.id; }
    std::string       ObjectName() const override { return identity_.name; }
    MachineObjectKind ObjectKind() const override { return MachineObjectKind::kSerialPort; }

    std::optional<InsertedItem>  Inserted() const override;
    std::vector<InsertableType> Accepted() const override;
    void Eject() override;

protected:
    void InsertAccepted(const std::string& type_id, const std::string& command_id,
                        const std::string& argument) override;

private:
    enum class Plugged { None, Modem, Forward, Tcp };

    static constexpr const char* kModemId   = "modem";
    static constexpr const char* kForwardId = "forward";
    static constexpr const char* kTcpId     = "tcp";

    void Replug(Plugged plugged, std::string host_port);
    void UnplugAt(uint64_t gen);
    void ReportFailure(uint64_t gen, const char* reason, uint32_t code);
    void SetPluggedLocked(Plugged plugged, std::string host_port, bool power_up);
    void Announce(Plugged before, Plugged after);

    CerfEmulator& emu_;
    SerialLine&    line_;
    ObjectIdentity identity_;

    mutable std::mutex              mtx_;
    Plugged                         plugged_ = Plugged::None;
    std::string                     host_port_;
    std::unique_ptr<SerialEndpoint> endpoint_;
    uint64_t                        generation_ = 0;
    bool                            shut_down_  = false;
    std::optional<std::string>      startup_tcp_;
};

}
