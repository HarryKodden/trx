#include "TestUtils.h"

#include <iostream>

namespace trx::test {

bool validateIfProcedure(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(!procedure.body.empty(), "branching procedure has no statements")) {
        return false;
    }

    const auto *ifStmt = std::get_if<trx::ast::IfStatement>(&procedure.body.front().node);
    if (!expect(ifStmt != nullptr, "branching first statement is not IF")) {
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

    return true;
}

bool validateWhileProcedure(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(!procedure.body.empty(), "looping procedure has no statements")) {
        return false;
    }

    const auto *whileStmt = std::get_if<trx::ast::WhileStatement>(&procedure.body.front().node);
    if (!expect(whileStmt != nullptr, "looping first statement is not WHILE")) {
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

    return true;
}

bool validateSwitchProcedure(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(!procedure.body.empty(), "switching procedure has no statements")) {
        return false;
    }

    const auto *switchStmt = std::get_if<trx::ast::SwitchStatement>(&procedure.body.front().node);
    if (!expect(switchStmt != nullptr, "switching first statement is not SWITCH")) {
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

    return true;
}

bool runControlStatementTests() {
    constexpr const char *source = R"TRX(
        TYPE SAMPLE {
            VALUE INTEGER;
            RESULT INTEGER;
        }

        FUNCTION branching(SAMPLE): SAMPLE {
            IF input.VALUE > 0 THEN {
                output.RESULT := input.VALUE;
            } ELSE {
                output.RESULT := 0;
            }
        }

        FUNCTION looping(SAMPLE): SAMPLE {
            WHILE input.VALUE > 0 {
                output.RESULT := output.RESULT + 1;
            }
        }

        FUNCTION switching(SAMPLE): SAMPLE {
            SWITCH input.VALUE {
                CASE 0 {
                    output.RESULT := 0;
                }
                CASE 1 {
                    output.RESULT := 1;
                }
                DEFAULT {
                    output.RESULT := -1;
                }
            }
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