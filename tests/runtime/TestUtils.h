#pragma once

#include "trx/ast/Nodes.h"
#include "trx/ast/Statements.h"
#include "trx/parsing/ParserDriver.h"
#include "trx/runtime/Interpreter.h"
#include "trx/runtime/TrxException.h"

#include <cstddef>
#include <iostream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <variant>

namespace trx::test {

void reportDiagnostics(const trx::parsing::ParserDriver &driver) {
    std::cerr << "Parsing failed with " << driver.diagnostics().messages().size() << " diagnostic messages:\n";
    for (const auto &diagnostic : driver.diagnostics().messages()) {
        std::cerr << "  - " << diagnostic.message << " at " << diagnostic.location.file << ":" << diagnostic.location.line << ":" << diagnostic.location.column << "\n";
    }
}

bool expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

const trx::ast::ProcedureDecl *findProcedure(const trx::ast::Module &module, std::string_view name) {
    for (const auto &declaration : module.declarations) {
        if (const auto *procedure = std::get_if<trx::ast::ProcedureDecl>(&declaration)) {
            if (procedure->name.baseName == name) {
                return procedure;
            }
        }
    }
    return nullptr;
}

const trx::ast::RecordDecl *findRecord(const trx::ast::Module &module, std::string_view name) {
    for (const auto &declaration : module.declarations) {
        if (const auto *record = std::get_if<trx::ast::RecordDecl>(&declaration)) {
            if (record->name.name == name) {
                return record;
            }
        }
    }
    return nullptr;
}

bool expectVariablePath(const trx::ast::VariableExpression &var, std::initializer_list<std::string_view> expectedPath) {
    if (var.path.size() != expectedPath.size()) {
        std::cerr << "Variable path size mismatch: expected " << expectedPath.size() << ", got " << var.path.size() << "\n";
        return false;
    }

    std::size_t index = 0;
    for (const auto &segment : expectedPath) {
        if (var.path[index].identifier != segment) {
            std::cerr << "Variable path segment " << index << " mismatch: expected '" << segment << "', got '" << var.path[index].identifier << "'\n";
            return false;
        }
        ++index;
    }

    return true;
}

} // namespace trx::test