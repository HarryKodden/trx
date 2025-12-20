#include "trx/runtime/SymbolTable.h"

namespace trx::runtime {

bool SymbolTable::insert(std::string name) {
    auto [it, inserted] = symbols_.try_emplace(std::move(name), true);
    return inserted;
}

bool SymbolTable::contains(std::string_view name) const noexcept {
    return symbols_.find(std::string(name)) != symbols_.end();
}

} // namespace trx::runtime
