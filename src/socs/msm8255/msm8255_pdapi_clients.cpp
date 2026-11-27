#include "msm8255_pdapi_clients.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace cerfcore {

bool Msm8255PdapiClients::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255PdapiClients::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { clients_.Clear(); });
}

uint32_t Msm8255PdapiClients::Create(uint32_t type) {
    Msm8255PdapiClient* client = clients_.Issue();
    if (!client) {
        emu_.Get<Fatal>().Die("msm8255 pdapi clients: client init of type %u after %u issued ids",
                              type, clients_.LastId());
    }
    client->type = type;
    return client->id;
}

bool Msm8255PdapiClients::Remove(uint32_t id) { return clients_.Remove(id); }

Msm8255PdapiClient* Msm8255PdapiClients::Find(uint32_t id) { return clients_.Find(id); }

void Msm8255PdapiClients::ReleaseClient(const Msm8255RpcEndpoint& client) {
    for (Msm8255PdapiClient& c : clients_.Clients()) {
        for (Msm8255PdapiRegistration& reg : c.regs) {
            if (reg.dest == client) {
                reg.callback = kRpcNoCallback;
            }
        }
    }
}

void Msm8255PdapiClients::SaveState(StateWriter& w) { clients_.Save(w, "pdapi"); }

void Msm8255PdapiClients::RestoreState(StateReader& r) { clients_.Restore(r, "pdapi"); }

REGISTER_SERVICE(Msm8255PdapiClients);

}
