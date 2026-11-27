#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

class Msm8255RpcServer;

struct Msm8255OncrpcCall {
    const uint8_t* body;
    uint32_t       body_bytes;
    uint32_t       xid;
    uint32_t       proc;
};

struct Msm8255OncrpcReply {
    uint32_t       xid;
    const uint8_t* results;
    uint32_t       result_bytes;
};

struct Msm8255OncrpcRoute {
    uint32_t out_pa;
    uint32_t out_cap;
    uint32_t self_pid;
    uint32_t self_cid;
    uint32_t peer_pid;
    uint32_t peer_cid;
    uint32_t xid;
};

class Msm8255OncrpcCodec : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    Msm8255OncrpcCall ParseCall(const Msm8255RpcServer& server, const uint8_t* payload,
                                uint32_t size);
    void RequireCallBytes(const Msm8255RpcServer& server, uint32_t proc,
                          uint32_t size, uint32_t want);
    uint32_t CallWord(const Msm8255OncrpcCall& call, uint32_t offset);
    std::vector<uint32_t> CallArgs(const Msm8255RpcServer& server, const Msm8255OncrpcCall& call,
                                   uint32_t size);
    void RequireArgWords(const Msm8255RpcServer& server, uint32_t proc,
                         const std::vector<uint32_t>& args, uint32_t want);
    void RequireArgWordsAtLeast(const Msm8255RpcServer& server, uint32_t proc,
                                const std::vector<uint32_t>& args, uint32_t want);
    void RequireBoolean(const Msm8255RpcServer& server, uint32_t proc, uint32_t word);
    void RequireNoCallback(const Msm8255RpcServer& server, uint32_t proc, uint32_t callback);
    uint32_t ReplyWord(const Msm8255OncrpcReply& reply, uint32_t offset);
    uint32_t ConsumeXdrString(const uint8_t* body, uint32_t size, uint32_t off,
                              uint32_t which, std::string* text = nullptr);
    uint32_t WriteAcceptedReply(const Msm8255OncrpcRoute& route,
                                const uint32_t* results, uint32_t result_words);
    uint32_t WriteOptionalReply(const Msm8255OncrpcRoute& route, uint32_t status,
                                uint32_t present, uint32_t value);
    uint32_t WriteCall(const Msm8255OncrpcRoute& route, uint32_t prog,
                       uint32_t vers, uint32_t proc, const uint32_t* args,
                       uint32_t arg_words);
    Msm8255OncrpcReply ParseAcceptedReply(const Msm8255RpcServer& server,
                                          const uint8_t* payload, uint32_t size);

private:
    uint32_t BodyWord(const uint8_t* body, uint32_t body_bytes, uint32_t offset);
    uint32_t WriteMessage(const Msm8255OncrpcRoute& route,
                          const std::vector<uint32_t>& body);
    uint32_t WriteReply(const Msm8255OncrpcRoute& route, uint32_t accept_stat,
                        const uint32_t* results, uint32_t result_words);
};

}
