#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/host/machine_object.h>

#include <string>

namespace cerfcore {

class GuestAdditionsDevice : public Service, public MachineObject {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void LiveResizeChanged();

    std::string       ObjectId() const override { return "guest_additions"; }
    static constexpr char kObjectName[] = "Guest Additions";
    std::string       ObjectName() const override { return kObjectName; }
    MachineObjectKind ObjectKind() const override { return MachineObjectKind::kGuestAdditions; }
};

}
