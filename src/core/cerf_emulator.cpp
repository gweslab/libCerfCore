#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/execution/jit_runner.h>

#include <typeinfo>

namespace cerfcore {

CerfEmulator::CerfEmulator(const CerfConfig& config, int argc, char** argv)
    : config_(config), argc_(argc), argv_(argv)
{
}

CerfEmulator::~CerfEmulator() {
    teardown_fatal_ = &Get<Fatal>();
    TearDown();
}

void CerfEmulator::OnServiceDestroy(const servicelocator::Service& service) {
    LOG(Boot, "[TEARDOWN] destroying %s\n", typeid(service).name());
}

void CerfEmulator::ShutDownService(servicelocator::Service& service) {
    teardown_fatal_->GuardTeardown([&service] { service.OnShutdown(); });
}

void CerfEmulator::RaiseLocatorError(const std::string& message) { Fatal::LocatorFailure(message.c_str()); }

void CerfEmulator::StartGuest() {
    LOG(Boot, "services up; starting jit...\n");
    Get<JitRunner>().Start();
}

void CerfEmulator::WaitForExit() {
    Get<JitRunner>().Join();
}

}
