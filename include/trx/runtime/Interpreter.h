#pragma once

#include "trx/ast/Nodes.h"
#include "trx/runtime/JsonValue.h"
#include "trx/runtime/DatabaseDriver.h"

#include <memory>
#include <vector>
#include <map>

namespace trx::runtime {

class Interpreter {
public:
    explicit Interpreter(const ast::Module &module, std::unique_ptr<DatabaseDriver> dbDriver = nullptr);
    ~Interpreter();

    std::optional<JsonValue> execute(const std::string &procedureName, const JsonValue &input);
    std::optional<JsonValue> execute(const std::string &procedureName, const JsonValue &input, const std::map<std::string, std::string> &pathParams);
    std::optional<JsonValue> execute(const ast::ProcedureDecl *procedure, const JsonValue &input, const std::map<std::string, std::string> &pathParams = {});

    const ast::ProcedureDecl* getRoutine(const std::string &name) const;
    const ast::RecordDecl* getRecord(const std::string &name) const;

    // Accessors for SQL operations
    DatabaseDriver& db() const { return *dbDriver_; }

    // Access to global variables
    std::unordered_map<std::string, JsonValue>& globalVariables() { return globalVariables_; }
    const std::unordered_map<std::string, JsonValue>& globalVariables() const { return globalVariables_; }

    // SQLCODE access
    double getSqlCode() const { return sqlCode_; }
    void setSqlCode(double code) { sqlCode_ = code; }

private:
    const ast::Module &module_;
    double sqlCode_{0.0}; // SQL return code
    std::unordered_map<std::string, const ast::ProcedureDecl*> routines_;
    std::unordered_map<std::string, const ast::RecordDecl*> records_;
    std::unordered_map<std::string, JsonValue> globalVariables_;
    std::unique_ptr<DatabaseDriver> dbDriver_;
};

} // namespace trx::runtime
