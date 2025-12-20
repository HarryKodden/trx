#include "trx/runtime/Interpreter.h"

#include "trx/ast/Expressions.h"
#include "trx/ast/Statements.h"

#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <variant>

namespace trx::runtime {

namespace {

template <typename... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <typename... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

struct ExecutionContext {
    std::unordered_map<std::string, JsonValue> variables;
};

JsonValue evaluateExpression(const trx::ast::ExpressionPtr &expression, ExecutionContext &context);
JsonValue resolveVariableValue(const trx::ast::VariableExpression &variable, const ExecutionContext &context);
JsonValue &resolveVariableTarget(const trx::ast::VariableExpression &variable, ExecutionContext &context);

const trx::ast::ProcedureDecl *findProcedure(const trx::ast::Module &module, const std::string &name) {
    for (const auto &declaration : module.declarations) {
        if (const auto *procedure = std::get_if<trx::ast::ProcedureDecl>(&declaration)) {
            if (procedure->name.name == name) {
                return procedure;
            }
        }
    }
    return nullptr;
}

JsonValue evaluateLiteral(const trx::ast::LiteralExpression &literal) {
    return std::visit(
        Overloaded{
            [](double value) { return JsonValue(value); },
            [](const std::string &value) { return JsonValue(value); },
            [](bool value) { return JsonValue(value); }
        },
        literal.value);
}

JsonValue evaluateExpression(const trx::ast::ExpressionPtr &expression, ExecutionContext &context) {
    if (!expression) {
        throw std::runtime_error("Attempted to evaluate empty expression");
    }

    return std::visit(
        Overloaded{
            [&](const trx::ast::LiteralExpression &literal) { return evaluateLiteral(literal); },
            [&](const trx::ast::VariableExpression &variable) { return resolveVariableValue(variable, context); },
            [&](const auto &) -> JsonValue {
                throw std::runtime_error("Expression type not supported by interpreter yet");
            }
        },
        expression->node);
}

JsonValue resolveVariableValue(const trx::ast::VariableExpression &variable, const ExecutionContext &context) {
    if (variable.path.empty()) {
        throw std::runtime_error("Variable expression is empty");
    }

    const JsonValue *current = nullptr;
    const auto rootIt = context.variables.find(variable.path.front().identifier);
    if (rootIt == context.variables.end()) {
        throw std::runtime_error("Unknown variable: " + variable.path.front().identifier);
    }
    current = &rootIt->second;

    for (std::size_t i = 1; i < variable.path.size(); ++i) {
        const auto &segment = variable.path[i];
        if (segment.subscript.has_value()) {
            throw std::runtime_error("Array subscripts are not supported yet");
        }
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

    return *current;
}

JsonValue &resolveVariableTarget(const trx::ast::VariableExpression &variable, ExecutionContext &context) {
    if (variable.path.empty()) {
        throw std::runtime_error("Variable expression is empty");
    }

    JsonValue &root = context.variables[variable.path.front().identifier];
    if (variable.path.size() == 1) {
        return root;
    }

    JsonValue *current = &root;
    for (std::size_t i = 1; i < variable.path.size(); ++i) {
        const auto &segment = variable.path[i];
        if (segment.subscript.has_value()) {
            throw std::runtime_error("Array subscripts are not supported yet");
        }

        if (!current->isObject()) {
            *current = JsonValue::object();
        }

        auto &object = current->asObject();
        if (i + 1 == variable.path.size()) {
            return object[segment.identifier];
        }

        current = &object[segment.identifier];
    }

    return *current; // Unreachable, but keeps compiler happy
}

void executeAssignment(const trx::ast::AssignmentStatement &assignment, ExecutionContext &context) {
    JsonValue value = evaluateExpression(assignment.value, context);
    JsonValue &target = resolveVariableTarget(assignment.target, context);
    target = std::move(value);
}

void executeStatement(const trx::ast::Statement &statement, ExecutionContext &context) {
    std::visit(
        Overloaded{
            [&](const trx::ast::AssignmentStatement &assignment) { executeAssignment(assignment, context); },
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

JsonValue JsonValue::object() {
    return JsonValue(Object{});
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

bool operator==(const JsonValue &lhs, const JsonValue &rhs) {
    return lhs.data == rhs.data;
}

bool operator!=(const JsonValue &lhs, const JsonValue &rhs) {
    return !(lhs == rhs);
}

Interpreter::Interpreter(const trx::ast::Module &module)
    : module_{module} {}

JsonValue Interpreter::execute(const std::string &procedureName, const JsonValue &input) const {
    const auto *procedure = findProcedure(module_, procedureName);
    if (!procedure) {
        throw std::runtime_error("Procedure not found: " + procedureName);
    }

    if (!procedure->input || !procedure->output) {
        throw std::runtime_error("Procedure must declare both input and output parameters");
    }

    if (procedure->input->type.name != procedure->output->type.name) {
        throw std::runtime_error("Procedure input and output types must match");
    }

    ExecutionContext context;
    context.variables.emplace("input", input);
    context.variables.emplace("output", JsonValue::object());

    executeStatements(procedure->body, context);

    const auto outIt = context.variables.find("output");
    if (outIt == context.variables.end()) {
        throw std::runtime_error("Procedure execution did not produce output");
    }

    return outIt->second;
}

} // namespace trx::runtime
