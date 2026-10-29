#pragma once

#include <type_traits>
#include <utility>

namespace cerfcore {

class CerfEmulator;

class InstanceGate {
public:
    InstanceGate() = default;
    explicit InstanceGate(CerfEmulator& emu) : emu_(&emu) {}

    void Attach(CerfEmulator& emu) { emu_ = &emu; }
    bool Attached() const { return emu_ != nullptr; }

    template <class Fn>
    bool Run(Fn&& fn) const {
        return RunBody([](void* body, CerfEmulator& emu) { (*static_cast<std::remove_reference_t<Fn>*>(body))(emu); },
                       const_cast<void*>(static_cast<const void*>(&fn)));
    }

    template <class Fn>
    auto Bind(Fn fn) const {
        return [gate = *this, fn = std::move(fn)](auto&&... args) mutable -> void
                   requires(std::is_invocable_v<Fn&, decltype(args)...> ||
                            std::is_invocable_v<Fn&, CerfEmulator&, decltype(args)...>)
        {
            gate.Run([&]([[maybe_unused]] CerfEmulator& emu) {
                if constexpr (std::is_invocable_v<Fn&, CerfEmulator&, decltype(args)...>)
                    fn(emu, std::forward<decltype(args)>(args)...);
                else
                    fn(std::forward<decltype(args)>(args)...);
            });
        };
    }

private:
    bool RunBody(void (*call)(void* body, CerfEmulator& emu), void* body) const;

    CerfEmulator* emu_ = nullptr;
};

}
