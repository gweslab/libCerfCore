#include "open_bus_window.h"

#include <cerfcore/core/cerf_emulator.h>
#include "peripheral_dispatcher.h"

namespace cerfcore {

bool OpenBusWindow::ShouldRegister() {
    const BoardContext& bd = emu_.Get<BoardContext>();
    return bd.GetBoardFamilyId() == WindowBoardFamilyId() && WindowOnBoard(bd);
}

bool OpenBusWindow::WindowOnBoard(const BoardContext&) const { return true; }

void OpenBusWindow::OnReady() {
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint8_t  OpenBusWindow::ReadByte (uint32_t addr) { return bus_.Read8 (WindowTag(), addr); }
uint16_t OpenBusWindow::ReadHalf (uint32_t addr) { return bus_.Read16(WindowTag(), addr); }
uint32_t OpenBusWindow::ReadWord (uint32_t addr) { return bus_.Read32(WindowTag(), addr); }

void OpenBusWindow::WriteByte (uint32_t addr, uint8_t)  { bus_.Write(WindowTag(), 8u,  addr); }
void OpenBusWindow::WriteHalf (uint32_t addr, uint16_t) { bus_.Write(WindowTag(), 16u, addr); }
void OpenBusWindow::WriteWord (uint32_t addr, uint32_t) { bus_.Write(WindowTag(), 32u, addr); }

}
