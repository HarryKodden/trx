#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <variant>
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

    bool isNull() const;
    bool isBool() const;
    bool isNumber() const;
    bool isString() const;

    bool asBool() const;
    double asNumber() const;
    const std::string& asString() const;
};

bool operator==(const JsonValue &lhs, const JsonValue &rhs);
bool operator!=(const JsonValue &lhs, const JsonValue &rhs);

template<class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

std::ostream &operator<<(std::ostream &os, const JsonValue &value);

} // namespace trx::runtime

