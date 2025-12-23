#include "TestUtils.h"

#include <iostream>

namespace trx::test {

bool runSwitchStatementTest() {
    std::cout << "Running switch statement test...\n";
    constexpr const char *source = R"TRX(
        TYPE INPUT_TYPE {
            VALUE INTEGER;
        }

        FUNCTION test_switch(INPUT_TYPE): INPUT_TYPE {
            SWITCH input.VALUE {
                CASE 1 {
                    output.VALUE := 10;
                }
                CASE 2 {
                    output.VALUE := 20;
                }
                CASE 3 {
                    output.VALUE := 30;
                }
                DEFAULT {
                    output.VALUE := -1;
                }
            }
            RETURN output;
        }
    )TRX";

    std::cout << "Source code to parse:\n" << source << "\n";

    trx::parsing::ParserDriver driver;
    std::cout << "Parsing source...\n";
    if (!driver.parseString(source, "switch_test.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    std::cout << "Parsing successful. Executing function with different inputs...\n";
    trx::runtime::Interpreter interpreter(driver.context().module(), nullptr); // Use default SQLite driver

    // Test case 1: value = 1
    {
        trx::runtime::JsonValue input = trx::runtime::JsonValue::object();
        input.asObject()["VALUE"] = trx::runtime::JsonValue(1.0);

        const auto outputOpt = interpreter.execute("test_switch", input);
        if (!outputOpt) {
            std::cerr << "Function did not return a value for input 1\n";
            return false;
        }
        const auto &output = *outputOpt;

        if (!output.isObject() || output.asObject().find("VALUE") == output.asObject().end() || 
            !std::holds_alternative<double>(output.asObject().at("VALUE").data) || 
            std::get<double>(output.asObject().at("VALUE").data) != 10.0) {
            std::cerr << "Expected output.VALUE 10 for input 1, got: " << output << "\n";
            return false;
        }
        std::cout << "Case 1 (value=1) passed: " << output << "\n";
    }

    // Test case 2: value = 2
    {
        trx::runtime::JsonValue input = trx::runtime::JsonValue::object();
        input.asObject()["VALUE"] = trx::runtime::JsonValue(2.0);

        const auto outputOpt = interpreter.execute("test_switch", input);
        if (!outputOpt) {
            std::cerr << "Function did not return a value for input 2\n";
            return false;
        }
        const auto &output = *outputOpt;

        if (!output.isObject() || output.asObject().find("VALUE") == output.asObject().end() || 
            !std::holds_alternative<double>(output.asObject().at("VALUE").data) || 
            std::get<double>(output.asObject().at("VALUE").data) != 20.0) {
            std::cerr << "Expected output.VALUE 20 for input 2, got: " << output << "\n";
            return false;
        }
        std::cout << "Case 2 (value=2) passed: " << output << "\n";
    }

    // Test case 3: value = 3
    {
        trx::runtime::JsonValue input = trx::runtime::JsonValue::object();
        input.asObject()["VALUE"] = trx::runtime::JsonValue(3.0);

        const auto outputOpt = interpreter.execute("test_switch", input);
        if (!outputOpt) {
            std::cerr << "Function did not return a value for input 3\n";
            return false;
        }
        const auto &output = *outputOpt;

        if (!output.isObject() || output.asObject().find("VALUE") == output.asObject().end() || 
            !std::holds_alternative<double>(output.asObject().at("VALUE").data) || 
            std::get<double>(output.asObject().at("VALUE").data) != 30.0) {
            std::cerr << "Expected output.VALUE 30 for input 3, got: " << output << "\n";
            return false;
        }
        std::cout << "Case 3 (value=3) passed: " << output << "\n";
    }

    // Test default case: value = 4
    {
        trx::runtime::JsonValue input = trx::runtime::JsonValue::object();
        input.asObject()["VALUE"] = trx::runtime::JsonValue(4.0);

        const auto outputOpt = interpreter.execute("test_switch", input);
        if (!outputOpt) {
            std::cerr << "Function did not return a value for input 4\n";
            return false;
        }
        const auto &output = *outputOpt;

        if (!output.isObject() || output.asObject().find("VALUE") == output.asObject().end() || 
            !std::holds_alternative<double>(output.asObject().at("VALUE").data) || 
            std::get<double>(output.asObject().at("VALUE").data) != -1.0) {
            std::cerr << "Expected output.VALUE -1 for input 4 (default), got: " << output << "\n";
            return false;
        }
        std::cout << "Default case (value=4) passed: " << output << "\n";
    }

    std::cout << "Switch statement test passed.\n";
    return true;
}

} // namespace trx::test

int main() {
    if (!trx::test::runSwitchStatementTest()) {
        std::cerr << "Switch statement test failed.\n";
        return 1;
    }

    std::cout << "All tests passed!\n";
    return 0;
}