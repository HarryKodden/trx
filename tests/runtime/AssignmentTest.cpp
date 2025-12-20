#include "trx/parsing/ParserDriver.h"
#include "trx/runtime/Interpreter.h"

#include <iostream>

int main() {
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
        std::cerr << "Parsing failed:\n";
        for (const auto &diagnostic : driver.diagnostics().messages()) {
            std::cerr << "  - " << diagnostic.message << "\n";
        }
        return 1;
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
        return 1;
    }

    return 0;
}
