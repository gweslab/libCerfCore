#include "msm8255_npa_clients.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>

#include <utility>

namespace cerfcore {

bool Msm8255NpaClients::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255NpaClients::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        clients_.Clear();
        names_.clear();
    });
}

void Msm8255NpaClients::RegisterResourceDriver(
    std::string name, std::function<void(uint32_t)> apply_level) {
    drivers_.push_back({std::move(name), std::move(apply_level)});
}

uint32_t Msm8255NpaClients::NameIndex(const std::string& resource) {
    for (uint32_t i = 0; i < names_.size(); ++i) {
        if (names_[i] == resource) return i;
    }
    names_.push_back(resource);
    return static_cast<uint32_t>(names_.size() - 1u);
}

uint32_t Msm8255NpaClients::IssueHandle(const std::string& resource) {
    Msm8255NpaClient* client = clients_.Issue();
    if (!client) {
        emu_.Get<Fatal>().Die("msm8255 npa clients: create-client after %u issued handles",
                              clients_.LastId());
    }
    client->name = NameIndex(resource);
    return client->id;
}

uint32_t Msm8255NpaClients::ApplyRequest(uint32_t handle, uint32_t request) {
    Msm8255NpaClient* self = clients_.Find(handle);
    if (!self) {
        emu_.Get<Fatal>().Die(
            "msm8255 npa clients: the issue-request call names client handle %u, and this "
            "peer has issued %u", handle, clients_.LastId());
    }
    self->request = request;
    self->issued  = 1u;

    for (const Msm8255NpaClient& other : clients_.Clients()) {
        if (other.id == self->id || other.name != self->name || other.issued == 0u) {
            continue;
        }
        emu_.Get<Fatal>().Die(
            "msm8255 npa clients: client handle %u requests %u of the resource "
            "client handle %u already requested %u of, and the rule this peer "
            "would aggregate them by is not modeled",
            handle, request, other.id, other.request);
    }
    for (const ResourceDriver& driver : drivers_) {
        if (driver.name == names_[self->name]) {
            driver.apply_level(request);
        }
    }
    return request;
}

void Msm8255NpaClients::SaveState(StateWriter& w) {
    w.Write<uint32_t>("npa_name_count", static_cast<uint32_t>(names_.size()));
    for (const std::string& name : names_) {
        w.Write<uint32_t>("npa_name_bytes", static_cast<uint32_t>(name.size()));
        w.WriteBytes("npa_name", name.data(), name.size());
    }
    clients_.Save(w, "npa");
}

void Msm8255NpaClients::RestoreState(StateReader& r) {
    uint32_t count = 0u;
    r.Read("npa_name_count", count);
    names_.assign(count, std::string{});
    for (std::string& name : names_) {
        uint32_t bytes = 0u;
        r.Read("npa_name_bytes", bytes);
        name.resize(bytes);
        r.ReadBytes("npa_name", name.data(), bytes);
    }
    clients_.Restore(r, "npa");
}

REGISTER_SERVICE(Msm8255NpaClients);

}
