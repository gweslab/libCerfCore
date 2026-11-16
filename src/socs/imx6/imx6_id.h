#pragma once

#include <string_view>

namespace cerfcore {

namespace SocId {

inline constexpr std::string_view Imx6dq  = "imx6dq";
inline constexpr std::string_view Imx6sdl = "imx6sdl";

inline bool IsImx6(std::string_view id) { return id == Imx6dq || id == Imx6sdl; }

}

}
