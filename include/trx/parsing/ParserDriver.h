#pragma once

#include "trx/parsing/ParserContext.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace trx::parsing {

class ParserDriver {
public:
    ParserDriver();

    bool parseFile(const std::filesystem::path &path);
    bool parseString(std::string_view input, std::string_view virtualFile = "<memory>");

    [[nodiscard]] ParserContext &context() noexcept;
    [[nodiscard]] const diagnostics::DiagnosticEngine &diagnostics() const noexcept;

    void setCurrentFile(std::string_view fileName);
    [[nodiscard]] std::string_view currentFile() const noexcept;

private:
    bool parseImpl(std::string_view content, std::string_view fileName);

    diagnostics::DiagnosticEngine diagnostics_{};
    ParserContext context_;
    std::string currentFile_{};
};

} // namespace trx::parsing
