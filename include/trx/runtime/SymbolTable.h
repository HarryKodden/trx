#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace trx::runtime {

class SymbolTable {
public:
    bool insert(std::string name);
    [[nodiscard]] bool contains(std::string_view name) const noexcept;

private:
    std::unordered_map<std::string, bool> symbols_{};
};

} // namespace trx::runtime
