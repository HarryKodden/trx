#include "TestUtils.h"

#include <iostream>

namespace trx::test {

bool validateIfProcedure(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(procedure.body.size() == 3, "branching procedure should have 3 statements")) {
        return false;
    }

    // Check variable declaration: var result SAMPLE := sample;
    const auto *varDecl = std::get_if<trx::ast::VariableDeclarationStatement>(&procedure.body[0].node);
    if (!expect(varDecl != nullptr, "branching first statement is not variable declaration")) {
        return false;
    }
    if (!expect(varDecl->name.name == "result", "branching variable name incorrect")) {
        return false;
    }

    // Check IF statement
    const auto *ifStmt = std::get_if<trx::ast::IfStatement>(&procedure.body[1].node);
    if (!expect(ifStmt != nullptr, "branching second statement is not IF")) {
        return false;
    }

    const auto *condition = std::get_if<trx::ast::BinaryExpression>(&ifStmt->condition->node);
    if (!expect(condition != nullptr, "branching condition is not binary") ||
        !expect(condition->op == trx::ast::BinaryOperator::Greater, "branching condition operator not Greater")) {
        return false;
    }

    if (!expect(condition->lhs != nullptr, "branching condition lhs missing")) {
        return false;
    }

    const auto *lhsVar = std::get_if<trx::ast::VariableExpression>(&condition->lhs->node);
    if (!expect(lhsVar != nullptr, "branching lhs not variable") ||
        !expect(lhsVar->path.size() == 2, "branching lhs variable path length")) {
        return false;
    }

    const auto *rhsLiteral = std::get_if<trx::ast::LiteralExpression>(&condition->rhs->node);
    const auto *rhsValue = rhsLiteral ? std::get_if<double>(&rhsLiteral->value) : nullptr;
    if (!expect(rhsLiteral != nullptr && rhsValue != nullptr && *rhsValue == 0.0, "branching rhs literal not 0")) {
        return false;
    }

    if (!expect(ifStmt->thenBranch.size() == 1, "branching then branch size incorrect") ||
        !expect(ifStmt->elseBranch.size() == 1, "branching else branch size incorrect")) {
        return false;
    }

    const auto *thenAssignment = std::get_if<trx::ast::AssignmentStatement>(&ifStmt->thenBranch.front().node);
    if (!expect(thenAssignment != nullptr, "branching then statement not assignment")) {
        return false;
    }

    const auto *elseAssignment = std::get_if<trx::ast::AssignmentStatement>(&ifStmt->elseBranch.front().node);
    if (!expect(elseAssignment != nullptr, "branching else statement not assignment")) {
        return false;
    }

    // Check RETURN statement
    const auto *returnStmt = std::get_if<trx::ast::ReturnStatement>(&procedure.body[2].node);
    if (!expect(returnStmt != nullptr, "branching third statement is not RETURN")) {
        return false;
    }
    if (!expect(returnStmt->value != nullptr, "branching return statement has no value")) {
        return false;
    }

    return true;
}

bool validateWhileProcedure(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(procedure.body.size() == 3, "looping procedure should have 3 statements")) {
        return false;
    }

    // Check variable declaration: var result SAMPLE := sample;
    const auto *varDecl = std::get_if<trx::ast::VariableDeclarationStatement>(&procedure.body[0].node);
    if (!expect(varDecl != nullptr, "looping first statement is not variable declaration")) {
        return false;
    }

    // Check WHILE statement
    const auto *whileStmt = std::get_if<trx::ast::WhileStatement>(&procedure.body[1].node);
    if (!expect(whileStmt != nullptr, "looping second statement is not WHILE")) {
        return false;
    }

    const auto *condition = std::get_if<trx::ast::BinaryExpression>(&whileStmt->condition->node);
    if (!expect(condition != nullptr, "looping condition is not binary")) {
        return false;
    }

    if (!expect(whileStmt->body.size() == 1, "looping body size incorrect")) {
        return false;
    }

    const auto *assignment = std::get_if<trx::ast::AssignmentStatement>(&whileStmt->body.front().node);
    if (!expect(assignment != nullptr, "looping body statement not assignment")) {
        return false;
    }

    // Check RETURN statement
    const auto *returnStmt = std::get_if<trx::ast::ReturnStatement>(&procedure.body[2].node);
    if (!expect(returnStmt != nullptr, "looping third statement is not RETURN")) {
        return false;
    }

    return true;
}

bool validateSwitchProcedure(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(procedure.body.size() == 3, "switching procedure should have 3 statements")) {
        return false;
    }

    // Check variable declaration: var result SAMPLE := sample;
    const auto *varDecl = std::get_if<trx::ast::VariableDeclarationStatement>(&procedure.body[0].node);
    if (!expect(varDecl != nullptr, "switching first statement is not variable declaration")) {
        return false;
    }

    // Check SWITCH statement
    const auto *switchStmt = std::get_if<trx::ast::SwitchStatement>(&procedure.body[1].node);
    if (!expect(switchStmt != nullptr, "switching second statement is not SWITCH")) {
        return false;
    }

    if (!expect(switchStmt->cases.size() == 2, "switching cases size incorrect")) {
        return false;
    }

    for (std::size_t index = 0; index < switchStmt->cases.size(); ++index) {
        const auto &caseClause = switchStmt->cases[index];
        if (!expect(caseClause.match != nullptr, "switching case match missing")) {
            return false;
        }
        const auto *literal = std::get_if<trx::ast::LiteralExpression>(&caseClause.match->node);
        const auto *value = literal ? std::get_if<double>(&literal->value) : nullptr;
        if (!expect(literal != nullptr && value != nullptr && *value == static_cast<double>(index), "switching case literal mismatch")) {
            return false;
        }
        if (!expect(caseClause.body.size() == 1, "switching case body size incorrect")) {
            return false;
        }
        const auto *assignment = std::get_if<trx::ast::AssignmentStatement>(&caseClause.body.front().node);
        if (!expect(assignment != nullptr, "switching case body not assignment")) {
            return false;
        }
    }

    if (!expect(switchStmt->defaultBranch.has_value(), "switching default branch missing")) {
        return false;
    }

    if (!expect(switchStmt->defaultBranch->size() == 1, "switching default body size incorrect")) {
        return false;
    }

    const auto *defaultAssignment = std::get_if<trx::ast::AssignmentStatement>(&switchStmt->defaultBranch->front().node);
    if (!expect(defaultAssignment != nullptr, "switching default statement not assignment")) {
        return false;
    }

    // Check RETURN statement
    const auto *returnStmt = std::get_if<trx::ast::ReturnStatement>(&procedure.body[2].node);
    if (!expect(returnStmt != nullptr, "switching third statement is not RETURN")) {
        return false;
    }

    return true;
}

bool validateForProcedure(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(procedure.body.size() == 3, "iterating procedure should have 3 statements")) {
        return false;
    }

    // Check variable declaration: var result SAMPLE := sample;
    const auto *varDecl = std::get_if<trx::ast::VariableDeclarationStatement>(&procedure.body[0].node);
    if (!expect(varDecl != nullptr, "iterating first statement is not variable declaration")) {
        return false;
    }

    // Check FOR statement
    const auto *forStmt = std::get_if<trx::ast::ForStatement>(&procedure.body[1].node);
    if (!expect(forStmt != nullptr, "iterating second statement is not FOR")) {
        return false;
    }

    // Check loop variable
    if (!expect(forStmt->loopVar.path.size() == 1, "iterating loop variable path length")) {
        return false;
    }
    if (!expect(forStmt->loopVar.path[0].identifier == "item", "iterating loop variable name")) {
        return false;
    }

    // Check collection (should be an array literal)
    const auto *arrayLiteral = std::get_if<trx::ast::ArrayLiteralExpression>(&forStmt->collection->node);
    if (!expect(arrayLiteral != nullptr, "iterating collection is not array literal")) {
        return false;
    }
    if (!expect(arrayLiteral->elements.size() == 3, "iterating array size incorrect")) {
        return false;
    }

    // Check body (should have one assignment)
    if (!expect(forStmt->body.size() == 1, "iterating body size incorrect")) {
        return false;
    }
    const auto *assignment = std::get_if<trx::ast::AssignmentStatement>(&forStmt->body.front().node);
    if (!expect(assignment != nullptr, "iterating body statement not assignment")) {
        return false;
    }

    // Check RETURN statement
    const auto *returnStmt = std::get_if<trx::ast::ReturnStatement>(&procedure.body[2].node);
    if (!expect(returnStmt != nullptr, "iterating third statement is not RETURN")) {
        return false;
    }

    return true;
}

bool runControlStatementTests() {
    constexpr const char *source = R"TRX(
        TYPE SAMPLE {
            VALUE INTEGER;
            RESULT INTEGER;
        }

        FUNCTION branching(sample: SAMPLE): SAMPLE {
            var result SAMPLE := sample;
            IF sample.VALUE > 0 {
                result.RESULT := sample.VALUE;
            } ELSE {
                result.RESULT := 0;
            }
            RETURN result;
        }

        FUNCTION looping(sample: SAMPLE): SAMPLE {
            var result SAMPLE := sample;
            WHILE sample.VALUE > 0 {
                result.RESULT := result.RESULT + 1;
            }
            RETURN result;
        }

        FUNCTION switching(sample: SAMPLE): SAMPLE {
            var result SAMPLE := sample;
            SWITCH sample.VALUE {
                CASE 0 {
                    result.RESULT := 0;
                }
                CASE 1 {
                    result.RESULT := 1;
                }
                DEFAULT {
                    result.RESULT := -1;
                }
            }
            RETURN result;
        }

        FUNCTION iterating(sample: SAMPLE): SAMPLE {
            var result SAMPLE := sample;
            FOR item IN [1, 2, 3] {
                result.RESULT := result.RESULT + item;
            }
            RETURN result;
        }
    )TRX";

    trx::parsing::ParserDriver driver;
    if (!driver.parseString(source, "control_cases.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    const auto *branching = findProcedure(driver.context().module(), "branching");
    if (!expect(branching != nullptr, "branching procedure not found") ||
        !validateIfProcedure(*branching)) {
        return false;
    }

    const auto *looping = findProcedure(driver.context().module(), "looping");
    if (!expect(looping != nullptr, "looping procedure not found") ||
        !validateWhileProcedure(*looping)) {
        return false;
    }

    const auto *switching = findProcedure(driver.context().module(), "switching");
    if (!expect(switching != nullptr, "switching procedure not found") ||
        !validateSwitchProcedure(*switching)) {
        return false;
    }

    const auto *iterating = findProcedure(driver.context().module(), "iterating");
    if (!expect(iterating != nullptr, "iterating procedure not found") ||
        !validateForProcedure(*iterating)) {
        return false;
    }

    return true;
}

} // namespace trx::test

int main() {
    if (!trx::test::runControlStatementTests()) {
        std::cerr << "Control statement tests failed.\n";
        return 1;
    }

    std::cout << "All tests passed!\n";
    return 0;
}