#include "TestUtils.h"

#include <iostream>

namespace trx::test {

bool runSqlPatternsTest() {
    std::cout << "Running SQL patterns test...\n";
    constexpr const char *source = R"TRX(
        ROUTINE test_cursor_patterns() : JSON {
            // Create test table
            EXEC SQL DROP TABLE IF EXISTS sqlpatternstest;
            EXEC SQL CREATE TABLE sqlpatternstest (
                id INTEGER PRIMARY KEY,
                name VARCHAR,
                value INTEGER,
                active BOOLEAN
            );

            // Insert test data
            EXEC SQL INSERT INTO sqlpatternstest (id, name, value, active) VALUES (1, 'Alice', 100, true);
            EXEC SQL INSERT INTO sqlpatternstest (id, name, value, active) VALUES (2, 'Bob', 200, false);
            EXEC SQL INSERT INTO sqlpatternstest (id, name, value, active) VALUES (3, 'Charlie', 300, true);
            EXEC SQL INSERT INTO sqlpatternstest (id, name, value, active) VALUES (4, 'Diana', 400, true);

            // Declare all variables at the top
            var results JSON := [];
            var active_param BOOLEAN := true;
            var param1 INTEGER := 2;
            var param2 INTEGER := 4;
            var active_flag BOOLEAN := true;
            var min_value INTEGER := 150;
            var is_active BOOLEAN := true;
            var id INTEGER := 0;
            var name VARCHAR := '';
            var value INTEGER := 0;
            var id2 INTEGER := 0;
            var name2 VARCHAR := '';
            var dept_name VARCHAR := '';
            var count INTEGER := 0;

            // Test 1: Traditional DECLARE with parameters, OPEN with USING
            EXEC SQL DECLARE cursor1 CURSOR FOR SELECT id FROM sqlpatternstest WHERE active = ?;
            EXEC SQL OPEN cursor1 USING :active_param;
            WHILE sqlcode = 0 {
                EXEC SQL FETCH cursor1 INTO :id;
                IF (sqlcode = 0) {
                    append(results, {"pattern": "DECLARE_OPEN", "id": id});
                }
            }
            EXEC SQL CLOSE cursor1;

            // Test 2: DECLARE once, OPEN multiple times with USING (new feature)
            EXEC SQL DECLARE cursor2 CURSOR FOR SELECT id, name FROM sqlpatternstest WHERE id = ?;

            // First OPEN with USING
            EXEC SQL OPEN cursor2 USING :param1;
            EXEC SQL FETCH cursor2 INTO :id2, :name2;

            IF (sqlcode = 0) {
                append(results, {"pattern": "DECLARE_OPEN_USING_1", "id": id2, "name": name2});
            }
            EXEC SQL CLOSE cursor2;

            // Second OPEN with USING (reuse same cursor declaration)
            EXEC SQL OPEN cursor2 USING :param2;
            EXEC SQL FETCH cursor2 INTO :id2, :name2;
            IF (sqlcode = 0) {
                append(results, {"pattern": "DECLARE_OPEN_USING_2", "id": id2, "name": name2});
            }
            EXEC SQL CLOSE cursor2;

            // Test 3: Multiple parameters in USING clause
            EXEC SQL DECLARE cursor3 CURSOR FOR SELECT id, name, value FROM sqlpatternstest WHERE active = ? AND value > ?;

            EXEC SQL OPEN cursor3 USING :active_flag, :min_value;
            WHILE sqlcode = 0 {
                EXEC SQL FETCH cursor3 INTO :id, :name, :value;
                IF (sqlcode = 0) {
                    append(results, {"pattern": "MULTI_PARAM_USING", "id": id, "name": name, "value": value});
                }
            }
            EXEC SQL CLOSE cursor3;

            // Test 4: Complex query with JOIN in cursor
            EXEC SQL DROP TABLE IF EXISTS dept_test;
            EXEC SQL CREATE TABLE dept_test (
                id INTEGER PRIMARY KEY,
                name VARCHAR
            );

            EXEC SQL INSERT INTO dept_test (id, name) VALUES (1, 'Engineering');
            EXEC SQL INSERT INTO dept_test (id, name) VALUES (2, 'Sales');

            EXEC SQL DECLARE cursor4 CURSOR FOR
                SELECT p.id, p.name, d.name as dept_name
                FROM sqlpatternstest p
                LEFT JOIN dept_test d ON p.id = d.id
                WHERE p.active = ?;

            EXEC SQL OPEN cursor4 USING :is_active;
            WHILE sqlcode = 0 {
                EXEC SQL FETCH cursor4 INTO :id, :name, :dept_name;
                IF (sqlcode = 0) {
                    append(results, {"pattern": "JOIN_CURSOR", "id": id, "name": name, "dept_name": dept_name});
                }
            }
            EXEC SQL CLOSE cursor4;

            // Test 5: SELECT INTO (single row select without cursor)
            var count INTEGER := 0;
            var active BOOLEAN := true;
            EXEC SQL SELECT count(*) INTO :count FROM sqlpatternstest WHERE active = :active;
            append(results, {"pattern": "SELECT_INTO", "count": count});

            RETURN {"data": results};
        }
    )TRX";

    std::cout << "Source code to parse:\n" << source << "\n";

    trx::parsing::ParserDriver driver;
    std::cout << "Parsing source...\n";
    if (!driver.parseString(source, "sql_patterns_test.trx")) {
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

    // Test cursor patterns
    std::cout << "Executing test_cursor_patterns...\n";
    const auto outputOpt1 = interpreter.execute("test_cursor_patterns", input);
    if (!outputOpt1) {
        std::cerr << "test_cursor_patterns should return a value\n";
        return false;
    }

    const auto& results1 = outputOpt1->asObject();
    if (results1.find("data") == results1.end()) {
        std::cerr << "test_cursor_patterns should return data\n";
        return false;
    }

    const auto& cursorResults = results1.at("data").asArray();
    std::cout << "Cursor patterns test returned " << cursorResults.size() << " results\n";

    // Verify we got expected patterns
    std::vector<std::string> expectedPatterns = {
        "DECLARE_OPEN", "DECLARE_OPEN_USING_1", "DECLARE_OPEN_USING_2",
        "MULTI_PARAM_USING", "JOIN_CURSOR", "SELECT_INTO"
    };

    for (const auto& pattern : expectedPatterns) {
        bool found = false;
        for (const auto& result : cursorResults) {
            const auto& obj = result.asObject();
            if (obj.find("pattern") != obj.end() &&
                obj.at("pattern").asString() == pattern) {
                found = true;
                break;
            }
        }
        if (!found) {
            std::cerr << "Expected pattern '" << pattern << "' not found in results\n";
            return false;
        }
    }

        std::cout << backend.name << " tests passed.\n";
    }

    std::cout << "All SQL patterns tests passed across all backends!\n";
    return true;
}

} // namespace trx::test

int main() {
    if (!trx::test::runSqlPatternsTest()) {
        std::cerr << "SQL patterns tests failed.\n";
        return 1;
    }

    std::cout << "All SQL patterns tests passed!\n";
    return 0;
}