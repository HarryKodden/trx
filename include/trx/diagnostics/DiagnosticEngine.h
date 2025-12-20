#pragma once

#include "trx/ast/SourceLocation.h"

#include <string>
#include <string_view>
#include <vector>

namespace trx::diagnostics {

struct Diagnostic {
    enum class Level { Error, Warning, Info };

    Level level{Level::Error};
    std::string message;
    ast::SourceLocation location{};
};

class DiagnosticEngine {
public:
    void report(Diagnostic::Level level, std::string message, ast::SourceLocation location = {});

    [[nodiscard]] bool hasErrors() const noexcept;
    [[nodiscard]] const std::vector<Diagnostic> &messages() const noexcept;

private:
    std::vector<Diagnostic> messages_{};
};

} // namespace trx::diagnostics
