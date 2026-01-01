#include "trx/runtime/JsonValue.h"

#include <iostream>

namespace trx::runtime {

JsonValue::JsonValue() : data(std::nullptr_t{}) {}
JsonValue::JsonValue(bool value) : data(value) {}
JsonValue::JsonValue(double value) : data(value) {}
JsonValue::JsonValue(int value) : data(static_cast<double>(value)) {}
JsonValue::JsonValue(std::string value) : data(std::move(value)) {}
JsonValue::JsonValue(const char *value) : data(value ? std::string(value) : std::string{}) {}
JsonValue::JsonValue(Object value) : data(std::move(value)) {}
JsonValue::JsonValue(Array value) : data(std::move(value)) {}

JsonValue JsonValue::object() {
    return JsonValue(Object{});
}

JsonValue JsonValue::array() {
    return JsonValue(Array{});
}

bool JsonValue::isObject() const {
    return std::holds_alternative<Object>(data);
}

JsonValue::Object &JsonValue::asObject() {
    return std::get<Object>(data);
}

const JsonValue::Object &JsonValue::asObject() const {
    return std::get<Object>(data);
}

bool JsonValue::isArray() const {
    return std::holds_alternative<Array>(data);
}

JsonValue::Array &JsonValue::asArray() {
    return std::get<Array>(data);
}

const JsonValue::Array &JsonValue::asArray() const {
    return std::get<Array>(data);
}

bool JsonValue::isNull() const {
    return std::holds_alternative<std::nullptr_t>(data);
}

bool JsonValue::isBool() const {
    return std::holds_alternative<bool>(data);
}

bool JsonValue::isNumber() const {
    return std::holds_alternative<double>(data);
}

bool JsonValue::isString() const {
    return std::holds_alternative<std::string>(data);
}

bool JsonValue::asBool() const {
    return std::get<bool>(data);
}

double JsonValue::asNumber() const {
    return std::get<double>(data);
}

const std::string& JsonValue::asString() const {
    return std::get<std::string>(data);
}

bool operator==(const JsonValue &lhs, const JsonValue &rhs) {
    return lhs.data == rhs.data;
}

bool operator!=(const JsonValue &lhs, const JsonValue &rhs) {
    return !(lhs == rhs);
}

std::ostream &operator<<(std::ostream &os, const JsonValue &value) {
    std::visit(
        Overloaded{
            [&](std::nullptr_t) { os << "null"; },
            [&](bool b) { os << (b ? "true" : "false"); },
            [&](double d) { os << d; },
            [&](const std::string &s) { os << '"' << s << '"'; },
            [&](const JsonValue::Object &obj) {
                os << '{';
                bool first = true;
                for (const auto &[key, val] : obj) {
                    if (!first) os << ',';
                    os << '"' << key << "\":" << val;
                    first = false;
                }
                os << '}';
            },
            [&](const JsonValue::Array &arr) {
                os << '[';
                bool first = true;
                for (const auto &val : arr) {
                    if (!first) os << ',';
                    os << val;
                    first = false;
                }
                os << ']';
            }
        },
        value.data);
    return os;
}

} // namespace trx::runtime

