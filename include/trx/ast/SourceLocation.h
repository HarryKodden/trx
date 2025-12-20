#pragma once

#include <cstddef>
#include <string_view>

namespace trx::ast {

struct SourceLocation {
    std::string_view file;
    std::size_t line{0};
    std::size_t column{0};
};

} // namespace trx::ast
