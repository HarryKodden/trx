#include "trx/ast/Statements.h"
#include "trx/parsing/ParserDriver.h"
#include "trx/runtime/Interpreter.h"

#include <iostream>
#include <string>
#include <string_view>
#include <variant>

namespace {

void reportDiagnostics(const trx::parsing::ParserDriver &driver) {
    std::cerr << "Parsing failed:\n";
    for (const auto &diagnostic : driver.diagnostics().messages()) {
        std::cerr << "  - " << diagnostic.message << "\n";
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
            if (procedure->name.name == name) {
                return procedure;
            }
        }
    }
    return nullptr;
}

bool runCopyProcedureTest() {
    constexpr const char *source = R"TRX(
        RECORD ADDRESS {
            STREET CHAR(64);
            ZIP INTEGER;
        }

        RECORD CUSTOMER {
            NAME CHAR(64);
            HOME ADDRESS;
        }

        PROCEDURE copy_customer(CUSTOMER, CUSTOMER) {
            output := input;
        }
    )TRX";

    trx::parsing::ParserDriver driver;
    if (!driver.parseString(source, "copy_customer.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    trx::runtime::Interpreter interpreter(driver.context().module());

    trx::runtime::JsonValue::Object home;
    home.emplace("STREET", trx::runtime::JsonValue("Main Street"));
    home.emplace("ZIP", trx::runtime::JsonValue(12345));

    trx::runtime::JsonValue::Object customer;
    customer.emplace("NAME", trx::runtime::JsonValue("Alice"));
    customer.emplace("HOME", trx::runtime::JsonValue(std::move(home)));

    trx::runtime::JsonValue input{std::move(customer)};

    const auto output = interpreter.execute("copy_customer", input);

    if (output != input) {
        std::cerr << "Output JSON did not match input JSON\n";
        return false;
    }

    return true;
}

bool validateNumericExpression(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(!procedure.body.empty(), "numeric_case missing body")) {
        return false;
    }

    const auto *assignment = std::get_if<trx::ast::AssignmentStatement>(&procedure.body.front().node);
    if (!expect(assignment != nullptr, "numeric_case first statement is not assignment")) {
        return false;
    }

    const auto *addExpr = std::get_if<trx::ast::BinaryExpression>(&assignment->value->node);
    if (!expect(addExpr != nullptr, "numeric_case expression is not binary add") ||
        !expect(addExpr->op == trx::ast::BinaryOperator::Add, "numeric_case root operator is not Add")) {
        return false;
    }

    const auto *multiplyExpr = std::get_if<trx::ast::BinaryExpression>(&addExpr->lhs->node);
    if (!expect(multiplyExpr != nullptr, "numeric_case left operand is not binary multiply") ||
        !expect(multiplyExpr->op == trx::ast::BinaryOperator::Multiply, "numeric_case left operand is not Multiply")) {
        return false;
    }

    const auto *leftVar = std::get_if<trx::ast::VariableExpression>(&multiplyExpr->lhs->node);
    if (!expect(leftVar != nullptr, "numeric_case lhs is not variable") ||
        !expect(leftVar->path.size() == 2, "numeric_case variable path not two segments") ||
        !expect(leftVar->path[0].identifier == "input", "numeric_case variable missing input segment") ||
        !expect(leftVar->path[1].identifier == "VALUE", "numeric_case variable missing VALUE segment")) {
        return false;
    }

    const auto *twoLiteral = std::get_if<trx::ast::LiteralExpression>(&multiplyExpr->rhs->node);
    if (!expect(twoLiteral != nullptr, "numeric_case multiplier is not literal")) {
        return false;
    }
    const auto *twoValue = std::get_if<double>(&twoLiteral->value);
    if (!expect(twoValue != nullptr && *twoValue == 2.0, "numeric_case multiplier literal not 2")) {
        return false;
    }

    const auto *addLiteral = std::get_if<trx::ast::LiteralExpression>(&addExpr->rhs->node);
    if (!expect(addLiteral != nullptr, "numeric_case addend is not literal")) {
        return false;
    }
    const auto *addValue = std::get_if<double>(&addLiteral->value);
    if (!expect(addValue != nullptr && *addValue == 5.0, "numeric_case add literal not 5")) {
        return false;
    }

    return true;
}

bool validateBooleanExpression(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(!procedure.body.empty(), "boolean_case missing body")) {
        return false;
    }

    const auto *assignment = std::get_if<trx::ast::AssignmentStatement>(&procedure.body.front().node);
    if (!expect(assignment != nullptr, "boolean_case first statement is not assignment")) {
        return false;
    }

    const auto *andExpr = std::get_if<trx::ast::BinaryExpression>(&assignment->value->node);
    if (!expect(andExpr != nullptr, "boolean_case expression is not binary") ||
        !expect(andExpr->op == trx::ast::BinaryOperator::And, "boolean_case root operator is not And")) {
        return false;
    }

    const auto *greaterExpr = std::get_if<trx::ast::BinaryExpression>(&andExpr->lhs->node);
    if (!expect(greaterExpr != nullptr, "boolean_case left operand is not comparison") ||
        !expect(greaterExpr->op == trx::ast::BinaryOperator::Greater, "boolean_case left operand is not Greater")) {
        return false;
    }

    const auto *leftVar = std::get_if<trx::ast::VariableExpression>(&greaterExpr->lhs->node);
    if (!expect(leftVar != nullptr, "boolean_case comparison lhs is not variable") ||
        !expect(leftVar->path.size() == 2, "boolean_case lhs variable path not two segments") ||
        !expect(leftVar->path[0].identifier == "input", "boolean_case lhs variable missing input segment") ||
        !expect(leftVar->path[1].identifier == "VALUE", "boolean_case lhs variable missing VALUE segment")) {
        return false;
    }

    const auto *tenLiteral = std::get_if<trx::ast::LiteralExpression>(&greaterExpr->rhs->node);
    const auto *tenValue = tenLiteral ? std::get_if<double>(&tenLiteral->value) : nullptr;
    if (!expect(tenLiteral != nullptr && tenValue != nullptr && *tenValue == 10.0, "boolean_case comparison rhs not 10")) {
        return false;
    }

    const auto *notExpr = std::get_if<trx::ast::UnaryExpression>(&andExpr->rhs->node);
    if (!expect(notExpr != nullptr, "boolean_case right operand is not unary") ||
        !expect(notExpr->op == trx::ast::UnaryOperator::Not, "boolean_case unary operator is not Not")) {
        return false;
    }

    const auto *equalExpr = std::get_if<trx::ast::BinaryExpression>(&notExpr->operand->node);
    if (!expect(equalExpr != nullptr, "boolean_case NOT operand is not comparison") ||
        !expect(equalExpr->op == trx::ast::BinaryOperator::Equal, "boolean_case NOT operand is not Equal")) {
        return false;
    }

    const auto *equalVar = std::get_if<trx::ast::VariableExpression>(&equalExpr->lhs->node);
    if (!expect(equalVar != nullptr, "boolean_case equality lhs is not variable") ||
        !expect(equalVar->path.size() == 2, "boolean_case equality variable path not two segments") ||
        !expect(equalVar->path[0].identifier == "input", "boolean_case equality variable missing input segment") ||
        !expect(equalVar->path[1].identifier == "VALUE", "boolean_case equality variable missing VALUE segment")) {
        return false;
    }

    const auto *zeroLiteral = std::get_if<trx::ast::LiteralExpression>(&equalExpr->rhs->node);
    const auto *zeroValue = zeroLiteral ? std::get_if<double>(&zeroLiteral->value) : nullptr;
    if (!expect(zeroLiteral != nullptr && zeroValue != nullptr && *zeroValue == 0.0, "boolean_case equality rhs not 0")) {
        return false;
    }

    return true;
}

bool validateBooleanLiteralExpression(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(!procedure.body.empty(), "bool_literal_case missing body")) {
        return false;
    }

    const auto *assignment = std::get_if<trx::ast::AssignmentStatement>(&procedure.body.front().node);
    if (!expect(assignment != nullptr, "bool_literal_case first statement is not assignment")) {
        return false;
    }

    const auto *orExpr = std::get_if<trx::ast::BinaryExpression>(&assignment->value->node);
    if (!expect(orExpr != nullptr, "bool_literal_case expression is not binary") ||
        !expect(orExpr->op == trx::ast::BinaryOperator::Or, "bool_literal_case root operator is not Or")) {
        return false;
    }

    const auto *leftLiteral = std::get_if<trx::ast::LiteralExpression>(&orExpr->lhs->node);
    const auto *leftValue = leftLiteral ? std::get_if<bool>(&leftLiteral->value) : nullptr;
    if (!expect(leftLiteral != nullptr && leftValue != nullptr && *leftValue, "bool_literal_case left literal not TRUE")) {
        return false;
    }

    const auto *rightLiteral = std::get_if<trx::ast::LiteralExpression>(&orExpr->rhs->node);
    const auto *rightValue = rightLiteral ? std::get_if<bool>(&rightLiteral->value) : nullptr;
    if (!expect(rightLiteral != nullptr && rightValue != nullptr && !*rightValue, "bool_literal_case right literal not FALSE")) {
        return false;
    }

    return true;
}

bool validateStringLiteralExpression(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(!procedure.body.empty(), "text_case missing body")) {
        return false;
    }

    const auto *assignment = std::get_if<trx::ast::AssignmentStatement>(&procedure.body.front().node);
    if (!expect(assignment != nullptr, "text_case first statement is not assignment")) {
        return false;
    }

    const auto *literal = std::get_if<trx::ast::LiteralExpression>(&assignment->value->node);
    if (!expect(literal != nullptr, "text_case expression is not literal")) {
        return false;
    }

    const auto *value = std::get_if<std::string>(&literal->value);
    if (!expect(value != nullptr && *value == "constant", "text_case literal value mismatch")) {
        return false;
    }

    return true;
}

bool runExpressionAstTests() {
    constexpr const char *source = R"TRX(
        RECORD SAMPLE {
            VALUE INTEGER;
            RESULT INTEGER;
            FLAG BOOLEAN;
            TEXT CHAR(32);
        }

        PROCEDURE numeric_case(SAMPLE, SAMPLE) {
            output.RESULT := input.VALUE * 2 + 5;
        }

        PROCEDURE boolean_case(SAMPLE, SAMPLE) {
            output.FLAG := input.VALUE > 10 AND NOT (input.VALUE = 0);
        }

        PROCEDURE bool_literal_case(SAMPLE, SAMPLE) {
            output.FLAG := TRUE OR FALSE;
        }

        PROCEDURE text_case(SAMPLE, SAMPLE) {
            output.TEXT := "constant";
        }
    )TRX";

    trx::parsing::ParserDriver driver;
    if (!driver.parseString(source, "expression_cases.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    const auto *numeric = findProcedure(driver.context().module(), "numeric_case");
    if (!expect(numeric != nullptr, "numeric_case procedure not found") ||
        !validateNumericExpression(*numeric)) {
        return false;
    }

    const auto *booleanProc = findProcedure(driver.context().module(), "boolean_case");
    if (!expect(booleanProc != nullptr, "boolean_case procedure not found") ||
        !validateBooleanExpression(*booleanProc)) {
        return false;
    }

    const auto *booleanLiteralProc = findProcedure(driver.context().module(), "bool_literal_case");
    if (!expect(booleanLiteralProc != nullptr, "bool_literal_case procedure not found") ||
        !validateBooleanLiteralExpression(*booleanLiteralProc)) {
        return false;
    }

    const auto *textProc = findProcedure(driver.context().module(), "text_case");
    if (!expect(textProc != nullptr, "text_case procedure not found") ||
        !validateStringLiteralExpression(*textProc)) {
        return false;
    }

    return true;
}

} // namespace

int main() {
    if (!runCopyProcedureTest()) {
        return 1;
    }

    if (!runExpressionAstTests()) {
        return 1;
    }

    return 0;
}
