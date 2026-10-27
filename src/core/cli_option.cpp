#include <cerfcore/core/cli_option.h>

namespace cerfcore {

std::string_view CliOptionName(std::string_view option) { return option.substr(0, option.find('=')); }

}
