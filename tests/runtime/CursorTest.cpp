#include "TestUtils.h"

#include <iostream>

namespace trx::test {

bool runCursorTest() {
    std::cout << "Running cursor test...\n";
    constexpr const char *source = R"TRX(
        PROCEDURE test_cursor() {
            EXEC SQL CREATE TABLE test_table (
                id INTEGER PRIMARY KEY,
                name VARCHAR(50),
                value INTEGER
            );

            EXEC SQL INSERT INTO test_table (id, name, value) VALUES (1, 'Alice', 100);
            EXEC SQL INSERT INTO test_table (id, name, value) VALUES (2, 'Bob', 200);
            EXEC SQL INSERT INTO test_table (id, name, value) VALUES (3, 'Charlie', 300);

            var record from table test_table;

            EXEC SQL DECLARE test_cursor CURSOR FOR
                SELECT id, name, value FROM test_table ORDER BY id;

            EXEC SQL OPEN test_cursor;

            var count INTEGER := 0;

            WHILE sqlcode = 0 {

                EXEC SQL FETCH test_cursor INTO :record.id, :record.name, :record.value;

                if (sqlcode = 0) {
                    trace("Fetched row: ID=" + record.id + ", Name=" + record.name + ", Value=" + record.value);

                    count := count + 1;
                }
            }

            EXEC SQL CLOSE test_cursor;

            trace("Total rows fetched: " + count);
            output := count;
        }
    )TRX";

    std::cout << "Source code to parse:\n" << source << "\n";

    trx::parsing::ParserDriver driver;
    std::cout << "Parsing source...\n";
    if (!driver.parseString(source, "cursor_test.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    std::cout << "Parsing successful. Executing procedure...\n";
    trx::runtime::Interpreter interpreter(driver.context().module(), nullptr); // Use default SQLite driver

    trx::runtime::JsonValue input{trx::runtime::JsonValue::Object{}};

    std::cout << "Executing procedure...\n";
    const auto outputOpt = interpreter.execute("test_cursor", input);
    if (!outputOpt) {
        std::cerr << "Procedure did not return a value\n";
        return false;
    }
    const auto &output = *outputOpt;

    // Check that we got 3 rows
    if (const auto* count = std::get_if<double>(&output.data)) {
        if (*count != 3.0) {
            std::cerr << "Expected 3 rows, got " << *count << "\n";
            return false;
        }
    } else {
        std::cerr << "Output is not a number\n";
        return false;
    }

    std::cout << "Cursor test passed.\n";
    return true;
}

} // namespace trx::test

int main() {
    if (!trx::test::runCursorTest()) {
        std::cerr << "Cursor test failed.\n";
        return 1;
    }

    std::cout << "All tests passed!\n";
    return 0;
}