#pragma once

#include "trx/ast/Nodes.h"

#include <ostream>
#include <string>
#include <unordered_map>
#include <variant>

namespace trx::runtime {

struct JsonValue {
    using Object = std::unordered_map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Object, Array>;

    JsonValue();
    explicit JsonValue(bool value);
    explicit JsonValue(double value);
    explicit JsonValue(int value);
    JsonValue(std::string value);
    JsonValue(const char *value);
    explicit JsonValue(Object value);
    explicit JsonValue(Array value);

    static JsonValue object();
    static JsonValue array();

    bool isObject() const;
    Object &asObject();
    const Object &asObject() const;

    bool isArray() const;
    Array &asArray();
    const Array &asArray() const;

    Storage data;
};

bool operator==(const JsonValue &lhs, const JsonValue &rhs);
bool operator!=(const JsonValue &lhs, const JsonValue &rhs);

std::ostream &operator<<(std::ostream &os, const JsonValue &value);

class Interpreter {
public:
    explicit Interpreter(const ast::Module &module);

    [[nodiscard]] JsonValue execute(const std::string &procedureName, const JsonValue &input) const;

private:
    const ast::Module &module_;
};

} // namespace trx::runtime
