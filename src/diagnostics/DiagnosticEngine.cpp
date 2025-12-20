#include "trx/diagnostics/DiagnosticEngine.h"

namespace trx::diagnostics {

void DiagnosticEngine::report(Diagnostic::Level level, std::string message, ast::SourceLocation location) {
    messages_.push_back(Diagnostic{.level = level, .message = std::move(message), .location = std::move(location)});
}

bool DiagnosticEngine::hasErrors() const noexcept {
    for (const auto &entry : messages_) {
        if (entry.level == Diagnostic::Level::Error) {
            return true;
        }
    }
    return false;
}

const std::vector<Diagnostic> &DiagnosticEngine::messages() const noexcept {
    return messages_;
}

} // namespace trx::diagnostics
