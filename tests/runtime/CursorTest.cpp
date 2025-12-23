#include "TestUtils.h"

#include <iostream>

namespace trx::test {

bool runCursorTest() {
    std::cout << "Running cursor test...\n";
    constexpr const char *source = R"TRX(
        FUNCTION test_json_list_from_cursor() : JSON {
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
            return results;
        }

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

        PROCEDURE test_cursor_json() {
            var cursor_results JSON := test_json_list_from_cursor();
            trace('cursor results fetched successfully');
            output := cursor_results;
        }
    )TRX";

    std::cout << "Source code to parse:\n" << source << "\n";

    trx::parsing::ParserDriver driver;
    std::cout << "Parsing source...\n";
    if (!driver.parseString(source, "cursor_test.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    std::cout << "Parsing successful. Executing procedures...\n";
    trx::runtime::Interpreter interpreter(driver.context().module(), nullptr); // Use default SQLite driver

    trx::runtime::JsonValue input{trx::runtime::JsonValue::Object{}};

    // Test basic cursor functionality
    std::cout << "Executing test_cursor...\n";
    const auto outputOpt1 = interpreter.execute("test_cursor", input);
    if (!outputOpt1) {
        std::cerr << "test_cursor procedure did not return a value\n";
        return false;
    }
    const auto &output1 = *outputOpt1;

    // Check that we got 3 rows
    if (const auto* count = std::get_if<double>(&output1.data)) {
        if (*count != 3.0) {
            std::cerr << "Expected 3 rows, got " << *count << "\n";
            return false;
        }
    } else {
        std::cerr << "Output is not a number\n";
        return false;
    }

    // Test cursor with JSON functionality
    std::cout << "Executing test_cursor_json...\n";
    const auto outputOpt2 = interpreter.execute("test_cursor_json", input);
    if (!outputOpt2) {
        std::cerr << "test_cursor_json procedure did not return a value\n";
        return false;
    }
    const auto &output2 = *outputOpt2;

    // Check that we got a JSON array with cursor results
    if (!output2.isArray()) {
        std::cerr << "Output is not a JSON array\n";
        return false;
    }
    
    const auto& results = output2.asArray();
    if (results.size() != 3) {
        std::cerr << "Expected 3 rows in results array, got " << results.size() << "\n";
        return false;
    }
    
    // Check first row
    if (!results[0].isObject()) {
        std::cerr << "First result is not a JSON object\n";
        return false;
    }
    
    const auto& firstRow = results[0].asObject();
    if (firstRow.find("name") == firstRow.end() || 
        !std::holds_alternative<std::string>(firstRow.at("name").data) ||
        std::get<std::string>(firstRow.at("name").data) != "Alice") {
        std::cerr << "First row name is not 'Alice'\n";
        return false;
    }
    
    if (firstRow.find("id") == firstRow.end() || 
        !std::holds_alternative<double>(firstRow.at("id").data) ||
        std::get<double>(firstRow.at("id").data) != 1.0) {
        std::cerr << "First row id is not 1\n";
        return false;
    }
    
    // Check that all rows have the expected structure
    for (size_t i = 0; i < results.size(); ++i) {
        if (!results[i].isObject()) {
            std::cerr << "Row " << i << " is not a JSON object\n";
            return false;
        }
        
        const auto& row = results[i].asObject();
        if (row.find("id") == row.end() || row.find("name") == row.end() || 
            row.find("age") == row.end() || row.find("active") == row.end()) {
            std::cerr << "Row " << i << " missing required fields\n";
            return false;
        }
    }

    std::cout << "Cursor tests passed.\n";
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