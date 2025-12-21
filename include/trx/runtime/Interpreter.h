#pragma once

#include "trx/ast/Nodes.h"

#include <sqlite3.h>
#include <unordered_map>
#include <vector>

namespace trx::runtime {

struct JsonValue {
    using Object = std::unordered_map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Object, Array>;

    Storage data;

    JsonValue();
    explicit JsonValue(bool value);
    explicit JsonValue(double value);
    explicit JsonValue(int value);
    JsonValue(std::string value);
    JsonValue(const char *value);
    explicit JsonValue(Object value);
    explicit JsonValue(Array value);

    JsonValue(const JsonValue &) = default;
    JsonValue(JsonValue &&) = default;
    JsonValue &operator=(const JsonValue &) = default;
    JsonValue &operator=(JsonValue &&) = default;

    static JsonValue object();
    static JsonValue array();

    bool isObject() const;
    Object &asObject();
    const Object &asObject() const;

    bool isArray() const;
    Array &asArray();
    const Array &asArray() const;
};

bool operator==(const JsonValue &lhs, const JsonValue &rhs);
bool operator!=(const JsonValue &lhs, const JsonValue &rhs);

std::ostream &operator<<(std::ostream &os, const JsonValue &value);

class Interpreter {
public:
    explicit Interpreter(const ast::Module &module);
    ~Interpreter();

    [[nodiscard]] JsonValue execute(const std::string &procedureName, const JsonValue &input) const;

    // Accessors for SQL operations
    sqlite3 *db() const { return db_; }
    std::unordered_map<std::string, sqlite3_stmt*> &cursors() const { return cursors_; }

private:
    const ast::Module &module_;
    std::unordered_map<std::string, const ast::ProcedureDecl*> procedures_;
    mutable sqlite3 *db_{nullptr};
    mutable std::unordered_map<std::string, sqlite3_stmt*> cursors_;
};

} // namespace trx::runtime
