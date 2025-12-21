#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wunused-result"

#include "trx/runtime/Interpreter.h"

#include "trx/ast/Expressions.h"
#include "trx/ast/Statements.h"

#include <sqlite3.h>
#include <iostream>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <cmath>

namespace trx::runtime {



namespace {

template <typename... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <typename... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

struct ExecutionContext {
    const Interpreter &interpreter;
    std::unordered_map<std::string, JsonValue> variables;
    bool returned{false};
    std::optional<JsonValue> returnValue;
};

JsonValue evaluateExpression(const trx::ast::ExpressionPtr &expression, ExecutionContext &context);
JsonValue resolveVariableValue(const trx::ast::VariableExpression &variable, ExecutionContext &context);
JsonValue &resolveVariableTarget(const trx::ast::VariableExpression &variable, ExecutionContext &context);

void executeStatements(const trx::ast::StatementList &statements, ExecutionContext &context);

void extractHostVariables(std::string &sql, ExecutionContext &context, std::unordered_map<std::string, JsonValue> &hostVars);
void bindHostVariables(sqlite3_stmt *stmt, const std::unordered_map<std::string, JsonValue> &hostVars);

JsonValue evaluateLiteral(const trx::ast::LiteralExpression &literal) {
    return std::visit(
        Overloaded{
            [](double value) { return JsonValue(value); },
            [](const std::string &value) { return JsonValue(value); },
            [](bool value) { return JsonValue(value); }
        },
        literal.value);
}

JsonValue evaluateUnary(const trx::ast::UnaryExpression &unary, ExecutionContext &context) {
    JsonValue operand = evaluateExpression(unary.operand, context);
    switch (unary.op) {
        case trx::ast::UnaryOperator::Positive:
            if (std::holds_alternative<double>(operand.data)) {
                return operand;
            }
            throw std::runtime_error("Positive operator requires numeric operand");
        case trx::ast::UnaryOperator::Negate:
            if (std::holds_alternative<double>(operand.data)) {
                return JsonValue(-std::get<double>(operand.data));
            }
            throw std::runtime_error("Negate operator requires numeric operand");
        case trx::ast::UnaryOperator::Not:
            if (std::holds_alternative<bool>(operand.data)) {
                return JsonValue(!std::get<bool>(operand.data));
            }
            throw std::runtime_error("Not operator requires boolean operand");
    }
    throw std::runtime_error("Unknown unary operator");
}

JsonValue evaluateBinary(const trx::ast::BinaryExpression &binary, ExecutionContext &context) {
    JsonValue lhs = evaluateExpression(binary.lhs, context);
    JsonValue rhs = evaluateExpression(binary.rhs, context);

    switch (binary.op) {
        case trx::ast::BinaryOperator::Add:
            if (std::holds_alternative<double>(lhs.data) && std::holds_alternative<double>(rhs.data)) {
                return JsonValue(std::get<double>(lhs.data) + std::get<double>(rhs.data));
            }
            if (std::holds_alternative<std::string>(lhs.data) && std::holds_alternative<std::string>(rhs.data)) {
                return JsonValue(std::get<std::string>(lhs.data) + std::get<std::string>(rhs.data));
            }
            throw std::runtime_error("Add operator requires compatible operands");
        case trx::ast::BinaryOperator::Subtract:
            if (std::holds_alternative<double>(lhs.data) && std::holds_alternative<double>(rhs.data)) {
                return JsonValue(std::get<double>(lhs.data) - std::get<double>(rhs.data));
            }
            throw std::runtime_error("Subtract operator requires numeric operands");
        case trx::ast::BinaryOperator::Multiply:
            if (std::holds_alternative<double>(lhs.data) && std::holds_alternative<double>(rhs.data)) {
                return JsonValue(std::get<double>(lhs.data) * std::get<double>(rhs.data));
            }
            throw std::runtime_error("Multiply operator requires numeric operands");
        case trx::ast::BinaryOperator::Divide:
            if (std::holds_alternative<double>(lhs.data) && std::holds_alternative<double>(rhs.data)) {
                double r = std::get<double>(rhs.data);
                if (r == 0.0) throw std::runtime_error("Division by zero");
                return JsonValue(std::get<double>(lhs.data) / r);
            }
            throw std::runtime_error("Divide operator requires numeric operands");
        case trx::ast::BinaryOperator::Modulo:
            if (std::holds_alternative<double>(lhs.data) && std::holds_alternative<double>(rhs.data)) {
                return JsonValue(std::fmod(std::get<double>(lhs.data), std::get<double>(rhs.data)));
            }
            throw std::runtime_error("Modulo operator requires numeric operands");
        case trx::ast::BinaryOperator::Equal:
            return JsonValue(lhs.data == rhs.data);
        case trx::ast::BinaryOperator::NotEqual:
            return JsonValue(lhs.data != rhs.data);
        case trx::ast::BinaryOperator::Less:
            if (std::holds_alternative<double>(lhs.data) && std::holds_alternative<double>(rhs.data)) {
                return JsonValue(std::get<double>(lhs.data) < std::get<double>(rhs.data));
            }
            if (std::holds_alternative<std::string>(lhs.data) && std::holds_alternative<std::string>(rhs.data)) {
                return JsonValue(std::get<std::string>(lhs.data) < std::get<std::string>(rhs.data));
            }
            throw std::runtime_error("Less operator requires comparable operands");
        case trx::ast::BinaryOperator::LessEqual:
            if (std::holds_alternative<double>(lhs.data) && std::holds_alternative<double>(rhs.data)) {
                return JsonValue(std::get<double>(lhs.data) <= std::get<double>(rhs.data));
            }
            if (std::holds_alternative<std::string>(lhs.data) && std::holds_alternative<std::string>(rhs.data)) {
                return JsonValue(std::get<std::string>(lhs.data) <= std::get<std::string>(rhs.data));
            }
            throw std::runtime_error("LessEqual operator requires comparable operands");
        case trx::ast::BinaryOperator::Greater:
            if (std::holds_alternative<double>(lhs.data) && std::holds_alternative<double>(rhs.data)) {
                return JsonValue(std::get<double>(lhs.data) > std::get<double>(rhs.data));
            }
            if (std::holds_alternative<std::string>(lhs.data) && std::holds_alternative<std::string>(rhs.data)) {
                return JsonValue(std::get<std::string>(lhs.data) > std::get<std::string>(rhs.data));
            }
            throw std::runtime_error("Greater operator requires comparable operands");
        case trx::ast::BinaryOperator::GreaterEqual:
            if (std::holds_alternative<double>(lhs.data) && std::holds_alternative<double>(rhs.data)) {
                return JsonValue(std::get<double>(lhs.data) >= std::get<double>(rhs.data));
            }
            if (std::holds_alternative<std::string>(lhs.data) && std::holds_alternative<std::string>(rhs.data)) {
                return JsonValue(std::get<std::string>(lhs.data) >= std::get<std::string>(rhs.data));
            }
            throw std::runtime_error("GreaterEqual operator requires comparable operands");
        case trx::ast::BinaryOperator::And:
            if (std::holds_alternative<bool>(lhs.data) && std::holds_alternative<bool>(rhs.data)) {
                return JsonValue(std::get<bool>(lhs.data) && std::get<bool>(rhs.data));
            }
            throw std::runtime_error("And operator requires boolean operands");
        case trx::ast::BinaryOperator::Or:
            if (std::holds_alternative<bool>(lhs.data) && std::holds_alternative<bool>(rhs.data)) {
                return JsonValue(std::get<bool>(lhs.data) || std::get<bool>(rhs.data));
            }
            throw std::runtime_error("Or operator requires boolean operands");
    }
    throw std::runtime_error("Unknown binary operator");
}

JsonValue evaluateFunctionCall(const trx::ast::FunctionCallExpression &call, ExecutionContext &context) {
    // For now, implement some built-in functions
    if (call.functionName == "length") {
        if (call.arguments.size() != 1) throw std::runtime_error("length function takes 1 argument");
        JsonValue arg = evaluateExpression(call.arguments[0], context);
        if (std::holds_alternative<std::string>(arg.data)) {
            return JsonValue(static_cast<double>(std::get<std::string>(arg.data).size()));
        }
        if (arg.isArray()) {
            return JsonValue(static_cast<double>(arg.asArray().size()));
        }
        throw std::runtime_error("length function requires string or array");
    }
    if (call.functionName == "substr") {
        if (call.arguments.size() != 3) throw std::runtime_error("substr function takes 3 arguments");
        JsonValue str = evaluateExpression(call.arguments[0], context);
        JsonValue start = evaluateExpression(call.arguments[1], context);
        JsonValue len = evaluateExpression(call.arguments[2], context);
        if (!std::holds_alternative<std::string>(str.data) || !std::holds_alternative<double>(start.data) || !std::holds_alternative<double>(len.data)) {
            throw std::runtime_error("substr arguments must be string, number, number");
        }
        const std::string &s = std::get<std::string>(str.data);
        size_t pos = static_cast<size_t>(std::get<double>(start.data));
        size_t length = static_cast<size_t>(std::get<double>(len.data));
        if (pos >= s.size()) return JsonValue("");
        return JsonValue(s.substr(pos, length));
    }
    // For user-defined procedures, could add recursive call, but for now throw
    throw std::runtime_error("Function not supported: " + call.functionName);
}

JsonValue evaluateBuiltin(const trx::ast::BuiltinExpression &builtin, ExecutionContext &context) {
    (void)context;
    switch (builtin.value) {
        case trx::ast::BuiltinValue::SqlCode:
            // Placeholder for SQL error code
            return JsonValue(0.0);
        case trx::ast::BuiltinValue::TrxDbms:
            return JsonValue("TRX");
        case trx::ast::BuiltinValue::TrxTime: {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            return JsonValue(std::ctime(&time));
        }
        case trx::ast::BuiltinValue::TrxCode:
            return JsonValue("TRX");
        case trx::ast::BuiltinValue::Date: {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm *tm = std::localtime(&time);
            char buf[11];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
            return JsonValue(buf);
        }
        case trx::ast::BuiltinValue::Stamp: {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            return JsonValue(static_cast<double>(time));
        }
        case trx::ast::BuiltinValue::Time: {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm *tm = std::localtime(&time);
            char buf[9];
            std::strftime(buf, sizeof(buf), "%H:%M:%S", tm);
            return JsonValue(buf);
        }
        case trx::ast::BuiltinValue::Week: {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm *tm = std::localtime(&time);
            return JsonValue(static_cast<double>(tm->tm_wday));
        }
        case trx::ast::BuiltinValue::WeekDay: {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm *tm = std::localtime(&time);
            return JsonValue(static_cast<double>(tm->tm_wday + 1)); // 1-7
        }
    }
    throw std::runtime_error("Unknown builtin");
}

JsonValue evaluateSqlFragment(const trx::ast::SqlFragmentExpression &sql, ExecutionContext &context) {
    std::string result;
    for (const auto &fragment : sql.fragments) {
        std::visit(
            Overloaded{
                [&](const std::string &str) { result += str; },
                [&](const trx::ast::VariableExpression &var) {
                    JsonValue val = resolveVariableValue(var, context);
                    // Convert to string
                    if (std::holds_alternative<double>(val.data)) {
                        result += std::to_string(std::get<double>(val.data));
                    } else if (std::holds_alternative<std::string>(val.data)) {
                        result += std::get<std::string>(val.data);
                    } else {
                        throw std::runtime_error("Cannot convert variable to string in SQL fragment");
                    }
                }
            },
            fragment.value);
    }
    return JsonValue(result);
}

struct ExpressionEvaluator {
    ExecutionContext &context;

    JsonValue operator()(const trx::ast::LiteralExpression &literal) const;
    JsonValue operator()(const trx::ast::VariableExpression &variable) const;
    JsonValue operator()(const trx::ast::UnaryExpression &unary) const;
    JsonValue operator()(const trx::ast::BinaryExpression &binary) const;
    JsonValue operator()(const trx::ast::FunctionCallExpression &call) const;
    JsonValue operator()(const trx::ast::BuiltinExpression &builtin) const;
    JsonValue operator()(const trx::ast::SqlFragmentExpression &sql) const;
};

JsonValue ExpressionEvaluator::operator()(const trx::ast::LiteralExpression &literal) const {
    return evaluateLiteral(literal);
}

JsonValue ExpressionEvaluator::operator()(const trx::ast::VariableExpression &variable) const {
    return resolveVariableValue(variable, context);
}

JsonValue ExpressionEvaluator::operator()(const trx::ast::UnaryExpression &unary) const {
    return evaluateUnary(unary, context);
}

JsonValue ExpressionEvaluator::operator()(const trx::ast::BinaryExpression &binary) const {
    return evaluateBinary(binary, context);
}

JsonValue ExpressionEvaluator::operator()(const trx::ast::FunctionCallExpression &call) const {
    return evaluateFunctionCall(call, context);
}

JsonValue ExpressionEvaluator::operator()(const trx::ast::BuiltinExpression &builtin) const {
    return evaluateBuiltin(builtin, context);
}

JsonValue ExpressionEvaluator::operator()(const trx::ast::SqlFragmentExpression &sql) const {
    return evaluateSqlFragment(sql, context);
}

JsonValue evaluateExpression(const trx::ast::ExpressionPtr &expression, ExecutionContext &context) {
    if (!expression) {
        throw std::runtime_error("Attempted to evaluate empty expression");
    }

    return std::visit(ExpressionEvaluator{context}, expression->node);
}

JsonValue resolveVariableValue(const trx::ast::VariableExpression &variable, ExecutionContext &context) {
    if (variable.path.empty()) {
        throw std::runtime_error("Variable expression is empty");
    }

    const JsonValue *current = nullptr;
    const auto rootIt = context.variables.find(variable.path.front().identifier);
    if (rootIt == context.variables.end()) {
        throw std::runtime_error("Unknown variable: " + variable.path.front().identifier);
    }
    current = &rootIt->second;

    for (std::size_t i = 0; i < variable.path.size(); ++i) {
        const auto &segment = variable.path[i];
        if (segment.subscript.has_value()) {
            if (!current->isArray()) {
                throw std::runtime_error("Attempted to subscript a non-array value");
            }
            const auto &array = current->asArray();
            JsonValue indexValue = evaluateExpression(segment.subscript.value(), context);
            if (!std::holds_alternative<double>(indexValue.data)) {
                throw std::runtime_error("Array index must be a number");
            }
            size_t index = static_cast<size_t>(std::get<double>(indexValue.data));
            if (index >= array.size()) {
                throw std::runtime_error("Array index out of bounds");
            }
            current = &array[index];
        } else {
            if (i > 0) {  // Not root
                if (!current->isObject()) {
                    throw std::runtime_error("Attempted to access field on non-object value");
                }
                const auto &object = current->asObject();
                const auto childIt = object.find(segment.identifier);
                if (childIt == object.end()) {
                    throw std::runtime_error("Unknown field: " + segment.identifier);
                }
                current = &childIt->second;
            }
        }
    }

    return *current;
}

JsonValue &resolveVariableTarget(const trx::ast::VariableExpression &variable, ExecutionContext &context) {
    if (variable.path.empty()) {
        throw std::runtime_error("Variable expression is empty");
    }

    JsonValue *current = nullptr;
    JsonValue &root = context.variables[variable.path.front().identifier];
    current = &root;

    for (std::size_t i = 0; i < variable.path.size(); ++i) {
        const auto &segment = variable.path[i];
        if (segment.subscript.has_value()) {
            if (!current->isArray()) {
                *current = JsonValue::array();
            }
            auto &array = current->asArray();
            JsonValue indexValue = evaluateExpression(segment.subscript.value(), context);
            if (!std::holds_alternative<double>(indexValue.data)) {
                throw std::runtime_error("Array index must be a number");
            }
            size_t index = static_cast<size_t>(std::get<double>(indexValue.data));
            if (index >= array.size()) {
                array.resize(index + 1);
            }
            current = &array[index];
        } else {
            if (i > 0) {  // Not root
                if (!current->isObject()) {
                    *current = JsonValue::object();
                }
                auto &object = current->asObject();
                current = &object[segment.identifier];
            }
        }
    }

    return *current;
}

void executeAssignment(const trx::ast::AssignmentStatement &assignment, ExecutionContext &context) {
    JsonValue value = evaluateExpression(assignment.value, context);
    JsonValue &target = resolveVariableTarget(assignment.target, context);
    target = std::move(value);
}

void executeIf(const trx::ast::IfStatement &ifStmt, ExecutionContext &context) {
    JsonValue cond = evaluateExpression(ifStmt.condition, context);
    if (std::holds_alternative<bool>(cond.data) && std::get<bool>(cond.data)) {
        executeStatements(ifStmt.thenBranch, context);
    } else {
        executeStatements(ifStmt.elseBranch, context);
    }
}

void executeWhile(const trx::ast::WhileStatement &whileStmt, ExecutionContext &context) {
    while (true) {
        JsonValue cond = evaluateExpression(whileStmt.condition, context);
        if (!std::holds_alternative<bool>(cond.data) || !std::get<bool>(cond.data)) break;
        executeStatements(whileStmt.body, context);
    }
}

void executeBlock(const trx::ast::BlockStatement &block, ExecutionContext &context) {
    executeStatements(block.statements, context);
}

void executeSwitch(const trx::ast::SwitchStatement &switchStmt, ExecutionContext &context) {
    JsonValue selector = evaluateExpression(switchStmt.selector, context);
    for (const auto &case_ : switchStmt.cases) {
        JsonValue match = evaluateExpression(case_.match, context);
        if (selector.data == match.data) {
            executeStatements(case_.body, context);
            return;
        }
    }
    if (switchStmt.defaultBranch) {
        executeStatements(*switchStmt.defaultBranch, context);
    }
}

void executeSort(const trx::ast::SortStatement &sortStmt, ExecutionContext &context) {
    JsonValue &arrayValue = resolveVariableTarget(sortStmt.array, context);
    if (!arrayValue.isArray()) {
        throw std::runtime_error("Sort target must be an array");
    }
    auto &array = arrayValue.asArray();
    if (sortStmt.keys.empty()) return; // No sort
    // For simplicity, sort by first key ascending
    const auto key = sortStmt.keys.front();
    std::sort(array.begin(), array.end(), [key](const JsonValue &a, const JsonValue &b) {
        if (!a.isObject() || !b.isObject()) return false;
        const auto &objA = a.asObject();
        const auto &objB = b.asObject();
        auto itA = objA.find(key.fieldName);
        auto itB = objB.find(key.fieldName);
        if (itA == objA.end() || itB == objB.end()) return false;
        // Compare based on type
        if (std::holds_alternative<double>(itA->second.data) && std::holds_alternative<double>(itB->second.data)) {
            double valA = std::get<double>(itA->second.data);
            double valB = std::get<double>(itB->second.data);
            return key.order > 0 ? valA < valB : valA > valB;
        }
        if (std::holds_alternative<std::string>(itA->second.data) && std::holds_alternative<std::string>(itB->second.data)) {
            const std::string &valA = std::get<std::string>(itA->second.data);
            const std::string &valB = std::get<std::string>(itB->second.data);
            return key.order > 0 ? valA < valB : valA > valB;
        }
        return false;
    });
}

void executeTrace(const trx::ast::TraceStatement &trace, ExecutionContext &context) {
    JsonValue val = evaluateExpression(trace.value, context);
    std::cout << "TRACE: " << val << std::endl;
}

void executeSystem(const trx::ast::SystemStatement &systemStmt, ExecutionContext &context) {
    JsonValue cmd = evaluateExpression(systemStmt.command, context);
    if (std::holds_alternative<std::string>(cmd.data)) {
        const std::string &command = std::get<std::string>(cmd.data);
        int result = std::system(command.c_str());
        (void)result;
    } else {
        throw std::runtime_error("System command must be a string");
    }
}

void executeBatch(const trx::ast::BatchStatement &batchStmt, ExecutionContext &context) {
    // For now, just print that batch is called
    std::cout << "BATCH: " << batchStmt.name;
    if (batchStmt.argument) {
        JsonValue arg = resolveVariableValue(*batchStmt.argument, context);
        std::cout << " with argument: " << arg;
    }
    std::cout << std::endl;
    // In a real implementation, this would execute the batch process
}

void executeCall(const trx::ast::CallStatement &callStmt, ExecutionContext &context) {
    JsonValue inputVal = callStmt.input ? resolveVariableValue(*callStmt.input, context) : JsonValue();
    JsonValue result = context.interpreter.execute(callStmt.name, inputVal);
    if (callStmt.output) {
        resolveVariableTarget(*callStmt.output, context) = result;
    }
}

struct ReturnException : std::exception {
    JsonValue value;
    ReturnException(JsonValue v) : value(std::move(v)) {}
};

void executeReturn(const trx::ast::ReturnStatement &returnStmt, ExecutionContext &context) {
    JsonValue val = evaluateExpression(returnStmt.value, context);
    throw ReturnException(val);
}

void executeValidate(const trx::ast::ValidateStatement &validateStmt, ExecutionContext &context) {
    JsonValue var = resolveVariableValue(validateStmt.variable, context);
    JsonValue ruleResult = evaluateExpression(validateStmt.rule, context);
    // Assume ruleResult is boolean
    bool valid = std::holds_alternative<bool>(ruleResult.data) && std::get<bool>(ruleResult.data);
    const trx::ast::ValidationOutcome &outcome = valid ? validateStmt.success : validateStmt.failure;
    // For now, just print
    std::cout << "VALIDATE: " << (valid ? "SUCCESS" : "FAILURE") << " code=" << outcome.code << " message=\"" << outcome.message << "\"" << std::endl;
    // Set final outcome
    // Perhaps set a variable, but for now, ignore
}

void extractHostVariables(std::string &sql, ExecutionContext &context, std::unordered_map<std::string, JsonValue> &hostVars) {
    size_t pos = 0;
    int paramIndex = 1;
    while ((pos = sql.find(':', pos)) != std::string::npos) {
        size_t end = pos + 1;
        while (end < sql.size() && (std::isalnum(sql[end]) || sql[end] == '.' || sql[end] == '_')) {
            ++end;
        }
        if (end > pos + 1) {
            std::string varName = sql.substr(pos + 1, end - pos - 1);
            // Replace :var with ? in SQL
            sql.replace(pos, end - pos, "?");
            
            // Extract the variable value
            try {
                trx::ast::VariableSegment segment{varName, std::nullopt};
                trx::ast::VariableExpression varExpr{{segment}};
                JsonValue value = resolveVariableValue(varExpr, context);
                hostVars[std::to_string(paramIndex)] = value;
                ++paramIndex;
            } catch (const std::exception &) {
                // Variable not found, keep as is
            }
        }
        pos = end;
    }
}

void bindHostVariables(sqlite3_stmt *stmt, const std::unordered_map<std::string, JsonValue> &hostVars) {
    for (const auto &[param, value] : hostVars) {
        int paramIndex = std::stoi(param);
        std::visit(Overloaded{
            [&](std::nullptr_t) { sqlite3_bind_null(stmt, paramIndex); },
            [&](bool b) { sqlite3_bind_int(stmt, paramIndex, b ? 1 : 0); },
            [&](double d) { sqlite3_bind_double(stmt, paramIndex, d); },
            [&](const std::string &s) { sqlite3_bind_text(stmt, paramIndex, s.c_str(), -1, SQLITE_TRANSIENT); },
            [&](const JsonValue::Object &) { /* Objects not supported */ },
            [&](const JsonValue::Array &) { /* Arrays not supported */ }
        }, value.data);
    }
}
std::string extractSelectFromDeclare(const std::string &declareSql) {
    std::string upper = declareSql;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    
    // Find "CURSOR FOR"
    size_t cursorForPos = upper.find("CURSOR FOR");
    if (cursorForPos == std::string::npos) {
        return declareSql; // Fallback
    }
    
    // Skip "CURSOR FOR" and whitespace
    size_t selectPos = cursorForPos + 10; // length of "CURSOR FOR"
    while (selectPos < declareSql.size() && std::isspace(declareSql[selectPos])) {
        ++selectPos;
    }
    
    return declareSql.substr(selectPos);
}
void executeSql(const trx::ast::SqlStatement &sqlStmt, ExecutionContext &context) {
    switch (sqlStmt.kind) {
        case trx::ast::SqlStatementKind::ExecImmediate: {
            std::string sql = sqlStmt.sql;
            // Replace host variables with ? placeholders for binding
            std::unordered_map<std::string, JsonValue> hostVars;
            extractHostVariables(sql, context, hostVars);
            
            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(context.interpreter.db(), sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                throw std::runtime_error("Failed to prepare SQL: " + std::string(sqlite3_errmsg(context.interpreter.db())));
            }
            
            // Bind parameters
            bindHostVariables(stmt, hostVars);
            
            // Execute the statement
            int rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
                std::string error = sqlite3_errmsg(context.interpreter.db());
                sqlite3_finalize(stmt);
                throw std::runtime_error("SQL execution failed: " + error);
            }
            
            sqlite3_finalize(stmt);
            std::cout << "SQL EXEC: " << sqlStmt.sql << std::endl;
            break;
        }
        
        case trx::ast::SqlStatementKind::DeclareCursor: {
            std::string sql = sqlStmt.sql;
            // Extract the SELECT statement from "DECLARE name CURSOR FOR select_stmt"
            std::string selectSql = extractSelectFromDeclare(sqlStmt.sql);
            
            std::unordered_map<std::string, JsonValue> hostVars;
            extractHostVariables(selectSql, context, hostVars);
            
            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(context.interpreter.db(), selectSql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                throw std::runtime_error("Failed to prepare cursor SQL: " + std::string(sqlite3_errmsg(context.interpreter.db())));
            }
            
            // Bind parameters for cursor declaration
            bindHostVariables(stmt, hostVars);
            
            context.interpreter.cursors()[sqlStmt.identifier] = stmt;
            std::cout << "SQL DECLARE CURSOR: " << sqlStmt.identifier << " AS " << selectSql << std::endl;
            break;
        }
        
        case trx::ast::SqlStatementKind::OpenCursor: {
            auto it = context.interpreter.cursors().find(sqlStmt.identifier);
            if (it == context.interpreter.cursors().end()) {
                throw std::runtime_error("Cursor not found: " + sqlStmt.identifier);
            }
            // Cursor is already prepared, just reset it
            sqlite3_reset(it->second);
            std::cout << "SQL OPEN CURSOR: " << sqlStmt.identifier << std::endl;
            break;
        }
        
        case trx::ast::SqlStatementKind::FetchCursor: {
            auto it = context.interpreter.cursors().find(sqlStmt.identifier);
            if (it == context.interpreter.cursors().end()) {
                throw std::runtime_error("Cursor not found: " + sqlStmt.identifier);
            }
            
            sqlite3_stmt *stmt = it->second;
            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW) {
                // Bind results to host variables
                for (size_t i = 0; i < sqlStmt.hostVariables.size() && i < static_cast<size_t>(sqlite3_column_count(stmt)); ++i) {
                    const auto &var = sqlStmt.hostVariables[i];
                    JsonValue value;
                    switch (sqlite3_column_type(stmt, i)) {
                        case SQLITE_INTEGER:
                            value = JsonValue(static_cast<double>(sqlite3_column_int(stmt, i)));
                            break;
                        case SQLITE_FLOAT:
                            value = JsonValue(sqlite3_column_double(stmt, i));
                            break;
                        case SQLITE_TEXT:
                            value = JsonValue(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, i))));
                            break;
                        case SQLITE_NULL:
                        default:
                            value = nullptr;
                            break;
                    }
                    resolveVariableTarget(var, context) = value;
                }
                std::cout << "SQL FETCH CURSOR: " << sqlStmt.identifier << " - row found" << std::endl;
            } else if (rc == SQLITE_DONE) {
                std::cout << "SQL FETCH CURSOR: " << sqlStmt.identifier << " - no more rows" << std::endl;
            } else {
                throw std::runtime_error("Cursor fetch failed: " + std::string(sqlite3_errmsg(context.interpreter.db())));
            }
            break;
        }
        
        case trx::ast::SqlStatementKind::CloseCursor: {
            auto it = context.interpreter.cursors().find(sqlStmt.identifier);
            if (it != context.interpreter.cursors().end()) {
                sqlite3_reset(it->second);
            }
            std::cout << "SQL CLOSE CURSOR: " << sqlStmt.identifier << std::endl;
            break;
        }
    }
}

void executeStatement(const trx::ast::Statement &statement, ExecutionContext &context) {
    std::visit(
        Overloaded{
            [&](const trx::ast::AssignmentStatement &assignment) { executeAssignment(assignment, context); },
            [&](const trx::ast::IfStatement &ifStmt) { executeIf(ifStmt, context); },
            [&](const trx::ast::WhileStatement &whileStmt) { executeWhile(whileStmt, context); },
            [&](const trx::ast::BlockStatement &block) { executeBlock(block, context); },
            [&](const trx::ast::SwitchStatement &switchStmt) { executeSwitch(switchStmt, context); },
            [&](const trx::ast::SortStatement &sortStmt) { executeSort(sortStmt, context); },
            [&](const trx::ast::TraceStatement &trace) { executeTrace(trace, context); },
            [&](const trx::ast::SystemStatement &systemStmt) { executeSystem(systemStmt, context); },
            [&](const trx::ast::BatchStatement &batchStmt) { executeBatch(batchStmt, context); },
            [&](const trx::ast::CallStatement &callStmt) { executeCall(callStmt, context); },
            [&](const trx::ast::ReturnStatement &returnStmt) { executeReturn(returnStmt, context); },
            [&](const trx::ast::ValidateStatement &validateStmt) { executeValidate(validateStmt, context); },
            [&](const trx::ast::SqlStatement &sqlStmt) { executeSql(sqlStmt, context); },
            [&](const auto &) {
                throw std::runtime_error("Statement type not supported by interpreter yet");
            }
        },
        statement.node);
}

void executeStatements(const trx::ast::StatementList &statements, ExecutionContext &context) {
    for (const auto &statement : statements) {
        executeStatement(statement, context);
    }
}

} // namespace

JsonValue::JsonValue() : data(std::nullptr_t{}) {}
JsonValue::JsonValue(bool value) : data(value) {}
JsonValue::JsonValue(double value) : data(value) {}
JsonValue::JsonValue(int value) : data(static_cast<double>(value)) {}
JsonValue::JsonValue(std::string value) : data(std::move(value)) {}
JsonValue::JsonValue(const char *value) : data(std::string(value)) {}
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

bool operator==(const JsonValue &lhs, const JsonValue &rhs) {
    return lhs.data == rhs.data;
}

bool operator!=(const JsonValue &lhs, const JsonValue &rhs) {
    return !(lhs == rhs);
}

Interpreter::Interpreter(const trx::ast::Module &module)
    : module_{module} {
    for (const auto &decl : module.declarations) {
        if (std::holds_alternative<ast::ProcedureDecl>(decl)) {
            const auto &proc = std::get<ast::ProcedureDecl>(decl);
            procedures_[proc.name.name] = &proc;
        }
    }
    
    // Open in-memory SQLite database
    if (sqlite3_open(":memory:", &db_) != SQLITE_OK) {
        throw std::runtime_error("Failed to open SQLite database");
    }
    
    // Create sample tables for testing
    const char *initSql = R"(
        CREATE TABLE CUSTOMERS (
            ID INTEGER PRIMARY KEY,
            NAME TEXT,
            VALUE INTEGER
        );
        INSERT INTO CUSTOMERS VALUES (1, 'Alice', 100);
        INSERT INTO CUSTOMERS VALUES (2, 'Bob', 200);
    )";
    
    char *errMsg = nullptr;
    if (sqlite3_exec(db_, initSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::string error = errMsg;
        sqlite3_free(errMsg);
        throw std::runtime_error("Failed to initialize database: " + error);
    }
}

Interpreter::~Interpreter() {
    // Clean up cursors
    for (auto &[name, stmt] : cursors_) {
        sqlite3_finalize(stmt);
    }
    cursors_.clear();
    
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

JsonValue Interpreter::execute(const std::string &procedureName, const JsonValue &input) const {
    auto it = procedures_.find(procedureName);
    if (it == procedures_.end()) {
        throw std::runtime_error("Procedure not found: " + procedureName);
    }
    const auto *procedure = it->second;

    if (!procedure->input || !procedure->output) {
        throw std::runtime_error("Procedure must declare both input and output parameters");
    }

    if (procedure->input->type.name != procedure->output->type.name) {
        throw std::runtime_error("Procedure input and output types must match");
    }

    ExecutionContext context{*this, {}, false, std::nullopt};
    context.variables.emplace("input", input);
    context.variables.emplace("output", JsonValue::object());

    try {
        executeStatements(procedure->body, context);
    } catch (const ReturnException &ret) {
        context.variables["output"] = ret.value;
    }

    const auto outIt = context.variables.find("output");
    if (outIt == context.variables.end()) {
        throw std::runtime_error("Procedure execution did not produce output");
    }

    return outIt->second;
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
                for (const auto &[k, v] : obj) {
                    if (!first) os << ',';
                    os << '"' << k << "\":" << v;
                    first = false;
                }
                os << '}';
            },
            [&](const JsonValue::Array &arr) {
                os << '[';
                bool first = true;
                for (const auto &v : arr) {
                    if (!first) os << ',';
                    os << v;
                    first = false;
                }
                os << ']';
            }
        },
        value.data);
    return os;
}

} // namespace trx::runtime
