#include "siemens_simatic_mobile_panels_gen2_hardware_info.h"

#include "../../core/unreachable.h"
#include <iterator>
#include <string>

namespace cerfcore {

namespace {

constexpr char kHex[] = "0123456789ABCDEF";

constexpr uint32_t kBacklightGammaBStub = 50u;
constexpr uint32_t kBacklightOffsetCStub = 0u;
constexpr uint32_t kBacklightFactorFStub = 1u;

void PushVarint(std::vector<uint8_t>& out, uint32_t value, bool sign_extended = false) {
    uint8_t group[6];
    std::size_t count = 0;
    do {
        group[count++] = static_cast<uint8_t>(value & 0x7Fu);
        value >>= 7u;
    } while (value != 0u);
    if (sign_extended && (group[count - 1u] & 0x40u) != 0u) group[count++] = 0u;
    while (count > 1u) out.push_back(static_cast<uint8_t>(0x80u | group[--count]));
    out.push_back(group[0]);
}

enum : uint8_t { kOmsBool = 1u, kOmsU8 = 2u, kOmsU16 = 3u, kOmsVarint = 4u, kOmsSignedVarint = 8u, kOmsString = 21u };

void PushAttributeHeader(std::vector<uint8_t>& out, uint32_t id, uint8_t type) {
    out.push_back(0xA3);
    PushVarint(out, id);
    out.push_back(0x00);
    out.push_back(type);
}

void PushAttribute(std::vector<uint8_t>& out, uint32_t id, uint8_t type, uint32_t value) {
    PushAttributeHeader(out, id, type);
    switch (type) {
    case kOmsBool: out.push_back(value != 0u ? 1u : 0u); break;
    case kOmsU8: out.push_back(static_cast<uint8_t>(value)); break;
    case kOmsU16:
        out.push_back(static_cast<uint8_t>(value >> 8u));
        out.push_back(static_cast<uint8_t>(value));
        break;
    case kOmsVarint: PushVarint(out, value); break;
    case kOmsSignedVarint: PushVarint(out, value, true); break;
    }
}

const char* MlfbFor(SiemensSimaticMobilePanelsGen2OpType op_type) {
    switch (op_type) {
    case SiemensSimaticMobilePanelsGen2OpType::Ktp400F: return "6AV2 125-2DB23-0AX0";
    case SiemensSimaticMobilePanelsGen2OpType::Ktp700: return "6AV2 125-2GB03-0AX0";
    case SiemensSimaticMobilePanelsGen2OpType::Ktp700F: return "6AV2 125-2GB23-0AX0";
    case SiemensSimaticMobilePanelsGen2OpType::Ktp900: return "6AV2 125-2JB03-0AX0";
    case SiemensSimaticMobilePanelsGen2OpType::Ktp900F: return "6AV2 125-2JB23-0AX0";
    case SiemensSimaticMobilePanelsGen2OpType::Tp1000F: return "6AV2 145-6KB20-0AS0";
    case SiemensSimaticMobilePanelsGen2OpType::Tp1000FRo: return "6AV2 145-6KB10-0AS0";
    case SiemensSimaticMobilePanelsGen2OpType::Ktp700FHw: return "6AV2 145-8GB00-0AA0";
    case SiemensSimaticMobilePanelsGen2OpType::Ktp700FArctic: return "6AV2 145-6GB06-0AA0";
    }
    CERF_UNREACHABLE();
}

const char* DeviceNameFor(SiemensSimaticMobilePanelsGen2OpType op_type) {
    switch (op_type) {
    case SiemensSimaticMobilePanelsGen2OpType::Ktp400F: return "KTP400F Mobile";
    case SiemensSimaticMobilePanelsGen2OpType::Ktp700: return "KTP700 Mobile";
    case SiemensSimaticMobilePanelsGen2OpType::Ktp700F: return "KTP700F Mobile";
    case SiemensSimaticMobilePanelsGen2OpType::Ktp900: return "KTP900 Mobile";
    case SiemensSimaticMobilePanelsGen2OpType::Ktp900F: return "KTP900F Mobile";
    case SiemensSimaticMobilePanelsGen2OpType::Tp1000F: return "TP1000F Mobile";
    case SiemensSimaticMobilePanelsGen2OpType::Tp1000FRo: return "TP1000F Mobile RO";
    case SiemensSimaticMobilePanelsGen2OpType::Ktp700FHw: return "KTP700F Mobile HW";
    case SiemensSimaticMobilePanelsGen2OpType::Ktp700FArctic: return "KTP700F Mobile Arctic";
    }
    CERF_UNREACHABLE();
}

void PushStringProperty(std::vector<uint8_t>& out, uint32_t id, const std::string& value) {
    PushAttributeHeader(out, id, kOmsString);
    PushVarint(out, static_cast<uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

constexpr uint32_t kProductDataDeviceName = 18452u;
constexpr uint32_t kProductDataMlfb = 18457u;

constexpr uint32_t kWidthMillimeter = 0u;
constexpr uint32_t kHeightMillimeter = 0u;
constexpr uint8_t kDataMode = 0u;
constexpr uint8_t kDataMappingMode = 0u;
constexpr uint8_t kDataLanes = 0u;
constexpr uint16_t kPowerSequenceDelay = 0u;
constexpr uint32_t kBacklightPwmFrequency = 0u;
constexpr uint16_t kBacklightPwmDutyCycle = 0u;

void PushDisplayObject(std::vector<uint8_t>& out, const SiemensSimaticMobilePanelsGen2Panel& panel) {
    const uint32_t h_period = panel.width + panel.timing.hsync_width + panel.timing.hstart_width + panel.timing.hend_width;
    const uint32_t v_period = panel.height + panel.timing.vsync_width + panel.timing.vstart_width + panel.timing.vend_width;
    PushAttribute(out, 18265u, kOmsVarint, panel.width);
    PushAttribute(out, 18266u, kOmsVarint, panel.height);
    PushAttribute(out, 18267u, kOmsVarint, kWidthMillimeter);
    PushAttribute(out, 18268u, kOmsVarint, kHeightMillimeter);
    PushAttribute(out, 18756u, kOmsSignedVarint, 33000u);
    PushAttribute(out, 18757u, kOmsSignedVarint, 0u);
    PushAttribute(out, 18758u, kOmsSignedVarint, kBacklightGammaBStub);
    PushAttribute(out, 18759u, kOmsSignedVarint, kBacklightOffsetCStub);
    PushAttribute(out, 18760u, kOmsSignedVarint, kBacklightFactorFStub);
    PushAttribute(out, 18911u, kOmsU8, 0u);
    PushAttribute(out, 18912u, kOmsU8, kDataMode);
    PushAttribute(out, 18913u, kOmsU8, kDataMappingMode);
    PushAttribute(out, 18914u, kOmsU8, panel.timing.data_bus_width);
    PushAttribute(out, 18915u, kOmsU8, kDataLanes);
    PushAttribute(out, 18916u, kOmsVarint, panel.timing.pixel_clock_hz);
    PushAttribute(out, 18917u, kOmsU16, v_period);
    PushAttribute(out, 18918u, kOmsU16, panel.timing.vsync_width);
    PushAttribute(out, 18919u, kOmsU16, panel.height);
    PushAttribute(out, 18920u, kOmsU16, panel.timing.vend_width);
    PushAttribute(out, 18921u, kOmsU16, panel.timing.vstart_width);
    PushAttribute(out, 18922u, kOmsBool, 0u);
    PushAttribute(out, 18923u, kOmsU16, h_period);
    PushAttribute(out, 18924u, kOmsU16, panel.timing.hsync_width);
    PushAttribute(out, 18925u, kOmsU16, panel.width);
    PushAttribute(out, 18926u, kOmsU16, panel.timing.hend_width);
    PushAttribute(out, 18927u, kOmsU16, panel.timing.hstart_width);
    PushAttribute(out, 18928u, kOmsBool, 0u);
    PushAttribute(out, 18929u, kOmsBool, 0u);
    PushAttribute(out, 18930u, kOmsBool, 0u);
    PushAttribute(out, 18931u, kOmsBool, 1u);
    for (uint32_t id = 18932u; id <= 18935u; ++id) PushAttribute(out, id, kOmsU16, kPowerSequenceDelay);
    PushAttribute(out, 18936u, kOmsVarint, kBacklightPwmFrequency);
    PushAttribute(out, 18937u, kOmsU16, kBacklightPwmDutyCycle);
    PushAttribute(out, 18938u, kOmsU16, kBacklightPwmDutyCycle);
}

}

std::vector<uint8_t> BuildSiemensSimaticMobilePanelsGen2HardwareInfoOms(const std::array<uint8_t, 6>& mac, SiemensSimaticMobilePanelsGen2OpType op_type,
                                                   const SiemensSimaticMobilePanelsGen2Panel& panel) {

    static constexpr uint8_t kRootObject[] = {0x03,
                                              0xA1, 0x01, 0x00, 0x00, 0x01, 0x81, 0x8E, 0x2F, 0x20, 0x00};

    static constexpr uint8_t kDisplayObject[] = {0xA1, 0x01, 0x00, 0x00, 0x02, 0x81, 0x8E, 0x3F, 0x20, 0x00,
                                                 0xA1, 0x01, 0x00, 0x00, 0x03, 0x81, 0x8E, 0x58, 0x20, 0x00};

    static constexpr uint8_t kOpTypeObject[] = {0xA1, 0x01, 0x00, 0x00, 0x06, 0x81, 0x92, 0x07, 0x20, 0x00};
    constexpr uint32_t kOpTypeFamily = 64u;

    static constexpr uint8_t kMacObjectPrefix[] = {0xA1, 0x01, 0x00, 0x00, 0x07, 0x81, 0x8E, 0x4C, 0x20, 0x00,
                                                   0xA1, 0x01, 0x00, 0x00, 0x08, 0x81, 0x92, 0x28, 0x20, 0x00};
    constexpr uint32_t kMacAddressString = 0x492Bu;

    std::vector<uint8_t> oms(std::begin(kRootObject), std::end(kRootObject));
    oms.insert(oms.end(), std::begin(kDisplayObject), std::end(kDisplayObject));
    PushDisplayObject(oms, panel);
    oms.push_back(0xA2);
    oms.push_back(0xA2);
    oms.insert(oms.end(), std::begin(kOpTypeObject), std::end(kOpTypeObject));
    PushAttribute(oms, 18698u, kOmsVarint, kOpTypeFamily);
    PushAttribute(oms, 18701u, kOmsVarint, static_cast<uint32_t>(op_type));
    oms.push_back(0xA2);

    static constexpr uint8_t kProductDataObject[] = {0xA1, 0x01, 0x00, 0x00, 0x09, 0x81, 0x8E, 0x3E, 0x20, 0x00};
    oms.insert(oms.end(), std::begin(kProductDataObject), std::end(kProductDataObject));
    PushStringProperty(oms, kProductDataDeviceName, DeviceNameFor(op_type));
    PushStringProperty(oms, kProductDataMlfb, MlfbFor(op_type));
    oms.push_back(0xA2);

    oms.insert(oms.end(), std::begin(kMacObjectPrefix), std::end(kMacObjectPrefix));
    std::string mac_text;
    for (std::size_t i = 0; i < mac.size(); ++i) {
        if (i != 0u) mac_text.push_back(':');
        mac_text.push_back(kHex[mac[i] >> 4u]);
        mac_text.push_back(kHex[mac[i] & 0x0Fu]);
    }
    PushStringProperty(oms, kMacAddressString, mac_text);
    oms.push_back(0xA2);
    oms.push_back(0xA2);
    oms.push_back(0xA2);
    return oms;
}

}
