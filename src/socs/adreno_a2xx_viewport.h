#pragma once
#include <cstdint>

namespace cerfcore {

class AdrenoA2xxViewportTransform {
public:
    explicit AdrenoA2xxViewportTransform(uint32_t value);

    uint32_t Value() const;
    bool     Valid() const;
    bool     WindowSpace() const;
    bool     Untransformed() const;
    bool     Scale(unsigned axis) const;
    bool     Offset(unsigned axis) const;
    bool     XyPremultiplied() const;
    bool     ZPremultiplied() const;
    bool     TakesReciprocalW() const;
    double   InverseW(float w) const;

private:
    uint32_t value_;
};

}
