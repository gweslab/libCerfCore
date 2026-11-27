#pragma once

#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

struct Msm8255RpcClientId {
    uint32_t id = 0u;

    template <typename F>
    static constexpr void Visit(Msm8255RpcClientId& c, F& field) {
        field("client_id", c.id);
    }
};

template <typename Client>
class Msm8255RpcClientTable {
    static_assert(StateVisitCoversAllBytes<Client>(
                      [](Client& c, StateFieldBytes& f) { Client::Visit(c, f); }),
                  "Client::Visit must name or skip every field of the client");

public:
    explicit Msm8255RpcClientTable(uint32_t max_id) : max_id_(max_id) {}

    Client* Issue() {
        if (last_id_ == max_id_) return nullptr;
        Client client{};
        client.id = ++last_id_;
        clients_.push_back(client);
        return &clients_.back();
    }

    Client* Find(uint32_t id) {
        const auto it = std::find_if(clients_.begin(), clients_.end(),
                                     [id](const Client& c) { return c.id == id; });
        return it == clients_.end() ? nullptr : &*it;
    }

    bool Remove(uint32_t id) {
        const auto it = std::find_if(clients_.begin(), clients_.end(),
                                     [id](const Client& c) { return c.id == id; });
        if (it == clients_.end()) return false;
        clients_.erase(it);
        return true;
    }

    void Clear() {
        clients_.clear();
        last_id_ = 0u;
    }

    uint32_t LastId() const { return last_id_; }

    std::vector<Client>& Clients() { return clients_; }

    void Save(StateWriter& w, const std::string& prefix) {
        w.Write((prefix + "_last_client").c_str(), last_id_);
        w.Write((prefix + "_client_count").c_str(), static_cast<uint32_t>(clients_.size()));
        StateWriteField field(w);
        for (Client& c : clients_) Client::Visit(c, field);
    }

    void Restore(StateReader& r, const std::string& prefix) {
        uint32_t count = 0u;
        r.Read((prefix + "_last_client").c_str(), last_id_);
        r.Read((prefix + "_client_count").c_str(), count);
        clients_.assign(count, Client{});
        StateReadField field(r);
        for (Client& c : clients_) Client::Visit(c, field);
    }

private:
    std::vector<Client> clients_;
    uint32_t            last_id_ = 0u;
    uint32_t            max_id_;
};

}
