#pragma once

#include "trx/ast/Nodes.h"
#include "trx/ast/SourceLocation.h"
#include "trx/diagnostics/DiagnosticEngine.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace trx::parsing {

class ParserContext {
public:
    explicit ParserContext(diagnostics::DiagnosticEngine &diagnostics);

    diagnostics::DiagnosticEngine &diagnosticEngine() noexcept;

    ast::Module &module() noexcept;

    void addInclude(std::string name, const ast::SourceLocation &location);
    void addConstant(std::string name, double value, const ast::SourceLocation &location);
    void addConstant(std::string name, std::string value, const ast::SourceLocation &location);
    void addRecord(ast::RecordDecl record);
    void addProcedure(ast::ProcedureDecl procedure);
    void addExternalProcedure(ast::ExternalProcedureDecl externalProcedure);
    void addTable(ast::TableDecl table);

    [[nodiscard]] ast::ParameterDecl makeParameter(std::string name, const ast::SourceLocation &location);
    void finalize();

private:
    diagnostics::DiagnosticEngine *diagnostics_;
    ast::Module module_{};
    std::unordered_map<std::string, ast::SourceLocation> recordIndex_;
    std::vector<std::pair<std::string, ast::SourceLocation>> pendingParameters_;
};

} // namespace trx::parsing
