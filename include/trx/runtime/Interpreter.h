#pragma once

#include "trx/ast/Nodes.h"
#include "trx/runtime/JsonValue.h"
#include "trx/runtime/DatabaseDriver.h"

#include <memory>
#include <vector>

namespace trx::runtime {

class Interpreter {
public:
    explicit Interpreter(const ast::Module &module, std::unique_ptr<DatabaseDriver> dbDriver = nullptr);
    ~Interpreter();

    JsonValue execute(const std::string &procedureName, const JsonValue &input) const;

    const ast::ProcedureDecl* getProcedure(const std::string &name) const;

    // Accessors for SQL operations
    DatabaseDriver& db() const { return *dbDriver_; }

private:
    const ast::Module &module_;
    std::unordered_map<std::string, const ast::ProcedureDecl*> procedures_;
    std::unique_ptr<DatabaseDriver> dbDriver_;
};

} // namespace trx::runtime
