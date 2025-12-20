#include "trx/parsing/ParserContext.h"

#include "trx/ast/Nodes.h"

#include <sstream>
#include <utility>

namespace trx::parsing {

ParserContext::ParserContext(diagnostics::DiagnosticEngine &diagnostics)
    : diagnostics_{&diagnostics} {}

diagnostics::DiagnosticEngine &ParserContext::diagnosticEngine() noexcept {
    return *diagnostics_;
}

ast::Module &ParserContext::module() noexcept {
    return module_;
}

void ParserContext::addInclude(std::string name, const ast::SourceLocation &location) {
    ast::IncludeDecl decl{.file = ast::Identifier{.name = std::move(name), .location = location}};
    module_.declarations.emplace_back(std::move(decl));
}

void ParserContext::addConstant(std::string name, double value, const ast::SourceLocation &location) {
    ast::ConstantDecl decl{.name = {.name = std::move(name), .location = location}, .value = value};
    module_.declarations.emplace_back(std::move(decl));
}

void ParserContext::addConstant(std::string name, std::string value, const ast::SourceLocation &location) {
    ast::ConstantDecl decl{.name = {.name = std::move(name), .location = location}, .value = std::move(value)};
    module_.declarations.emplace_back(std::move(decl));
}

void ParserContext::addRecord(ast::RecordDecl record) {
    const std::string recordName = record.name.name;
    const auto [it, inserted] = recordIndex_.emplace(recordName, record.name.location);
    if (!inserted) {
        std::ostringstream message;
        message << "Record '" << recordName << "' already defined";
        diagnosticEngine().report(diagnostics::Diagnostic::Level::Error, message.str(), record.name.location);
    }

    module_.declarations.emplace_back(std::move(record));
}

void ParserContext::addProcedure(ast::ProcedureDecl procedure) {
    module_.declarations.emplace_back(std::move(procedure));
}

void ParserContext::addExternalProcedure(ast::ExternalProcedureDecl externalProcedure) {
    module_.declarations.emplace_back(std::move(externalProcedure));
}

ast::ParameterDecl ParserContext::makeParameter(std::string name, const ast::SourceLocation &location) {
    ast::ParameterDecl parameter{.type = {.name = std::move(name), .location = location}};

    if (recordIndex_.find(parameter.type.name) == recordIndex_.end()) {
        pendingParameters_.emplace_back(parameter.type.name, location);
    }

    return parameter;
}

void ParserContext::finalize() {
    for (const auto &[typeName, location] : pendingParameters_) {
        if (recordIndex_.find(typeName) != recordIndex_.end()) {
            continue;
        }

        std::ostringstream message;
        message << "Procedure parameter references undefined record '" << typeName << "'";
        diagnosticEngine().report(diagnostics::Diagnostic::Level::Error, message.str(), location);
    }

    pendingParameters_.clear();
}

} // namespace trx::parsing
