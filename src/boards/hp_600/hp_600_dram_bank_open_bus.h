#pragma once

#include "../../peripherals/open_bus_window.h"

#include <cstdint>
#include <string_view>

namespace cerfcore {

class Hp600DramBankOpenBus : public OpenBusWindow {
public:
    using OpenBusWindow::OpenBusWindow;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;

protected:
    virtual uint32_t         BankPaBase() const = 0;
    virtual std::string_view ProbingSocId() const = 0;

    std::string_view WindowBoardFamilyId() const override;
    bool             WindowOnBoard(const BoardContext& bd) const override;

private:
    uint32_t BackedBytes() const;
};

}
