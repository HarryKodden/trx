#include "TestUtils.h"

#include <iostream>

namespace trx::test {

bool runCopyProcedureTest() {
    std::cout << "Running copy procedure test...\n";
    constexpr const char *source = R"TRX(
        TYPE ADDRESS {
            STREET CHAR(64);
            ZIP INTEGER;
        }

        TYPE CUSTOMER {
            NAME CHAR(64);
            HOME ADDRESS;
        }

        FUNCTION copy_customer(customer: CUSTOMER): CUSTOMER {
            var result CUSTOMER := customer;
            RETURN result;
        }
    )TRX";

    std::cout << "Source code to parse:\n" << source << "\n";

    trx::parsing::ParserDriver driver;
    std::cout << "Parsing source...\n";
    if (!driver.parseString(source, "copy_customer.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    std::cout << "Parsing successful. Checking module contents...\n";
    trx::runtime::Interpreter interpreter(driver.context().module(), nullptr); // Use default SQLite driver

    trx::runtime::JsonValue::Object home;
    home.emplace("STREET", trx::runtime::JsonValue("Main Street"));
    home.emplace("ZIP", trx::runtime::JsonValue(12345));

    trx::runtime::JsonValue::Object customer;
    customer.emplace("NAME", trx::runtime::JsonValue("Alice"));
    customer.emplace("HOME", trx::runtime::JsonValue(std::move(home)));

    trx::runtime::JsonValue input{std::move(customer)};

    std::cout << "Executing procedure...\n";
    const auto outputOpt = interpreter.execute("copy_customer", input);
    if (!outputOpt) {
        std::cerr << "Procedure did not return a value\n";
        return false;
    }
    const auto &output = *outputOpt;

    if (output != input) {
        std::cerr << "Output JSON did not match input JSON\n";
        return false;
    }

    std::cout << "Copy procedure test passed.\n";
    return true;
}

} // namespace trx::test

int main() {
    if (!trx::test::runCopyProcedureTest()) {
        std::cerr << "Copy procedure test failed.\n";
        return 1;
    }

    std::cout << "All tests passed!\n";
    return 0;
}