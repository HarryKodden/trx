#include "TestUtils.h"

#include <iostream>

namespace trx::test {

bool runCursorTest() {
    std::cout << "Running cursor test...\n";
    constexpr const char *source = R"TRX(
        ROUTINE test_json_list_from_cursor() : JSON {
            EXEC SQL DROP TABLE IF EXISTS cursor_test_table;
            EXEC SQL CREATE TABLE cursor_test_table (
                id INTEGER PRIMARY KEY,
                name VARCHAR(50),
                age INTEGER,
                active BOOLEAN
            );

            EXEC SQL INSERT INTO cursor_test_table (id, name, age, active) VALUES (1, 'Alice', 25, true);
            EXEC SQL INSERT INTO cursor_test_table (id, name, age, active) VALUES (2, 'Bob', 30, false);
            EXEC SQL INSERT INTO cursor_test_table (id, name, age, active) VALUES (3, 'Charlie', 35, true);

            EXEC SQL DECLARE json_cursor CURSOR FOR
                SELECT id, name, age, active FROM cursor_test_table ORDER BY id;

            EXEC SQL OPEN json_cursor;

            var results JSON := [];  // Initialize as empty JSON array
            var row_data JSON;

            WHILE sqlcode = 0 {
                var id INTEGER;
                var name CHAR(50);
                var age INTEGER;
                var active BOOLEAN;

                EXEC SQL FETCH json_cursor INTO :id, :name, :age, :active;

                if (sqlcode = 0) {
                    // Create a JSON object for this row
                    row_data := {
                        "id": id,
                        "name": name,
                        "age": age,
                        "active": active
                    };
                    
                    // Append the row object to the results array
                    append(results, row_data);
                    
                    trace("Added row to JSON list: " + row_data);
                }
            }

            EXEC SQL CLOSE json_cursor;

            trace("Total rows in JSON list: " + length(results));
            RETURN results;
        }

        ROUTINE test_cursor() {
            EXEC SQL DROP TABLE IF EXISTS test_table;
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
            // No output assignment needed for procedure
        }

        ROUTINE test_cursor_json() {
            var cursor_results JSON := test_json_list_from_cursor();
            trace('cursor results fetched successfully');
            // No output assignment needed for procedure
        }
    )TRX";

    std::cout << "Source code to parse:\n" << source << "\n";

    trx::parsing::ParserDriver driver;
    std::cout << "Parsing source...\n";
    if (!driver.parseString(source, "cursor_test.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    std::cout << "Parsing successful.\n";

    const auto &module = driver.context().module();
    
    // Run tests against all configured database backends
    const auto backends = getTestDatabaseBackends();
    std::cout << "Testing against " << backends.size() << " database backend(s)...\n";
    
    for (const auto& backend : backends) {
        std::cout << "\n=== Testing with " << backend.name << " ===\n";
        
        auto dbDriver = createTestDatabaseDriver(backend);
        trx::runtime::Interpreter interpreter(module, std::move(dbDriver));

        trx::runtime::JsonValue input{trx::runtime::JsonValue::Object{}};

        // Test basic cursor functionality
        std::cout << "Executing test_cursor...\n";
        const auto outputOpt1 = interpreter.execute("test_cursor", input);
        if (outputOpt1) {
            std::cerr << "test_cursor procedure should not return a value\n";
            return false;
        }

        // Test cursor with JSON functionality
        std::cout << "Executing test_cursor_json...\n";
        const auto outputOpt2 = interpreter.execute("test_cursor_json", input);
        if (outputOpt2) {
            std::cerr << "test_cursor_json procedure should not return a value\n";
            return false;
        }

        std::cout << backend.name << " tests passed.\n";
    }

    std::cout << "All cursor tests passed across all backends.\n";
    return true;
}

} // namespace trx::test

int main() {
    if (!trx::test::runCursorTest()) {
        std::cerr << "Cursor tests failed.\n";
        return 1;
    }

    std::cout << "All cursor tests passed!\n";
    return 0;
}