#include "TestUtils.h"

#include <iostream>
#include <string>
#include <vector>

namespace trx::test {

bool runComprehensiveDatabaseTest() {
    std::cout << "Running comprehensive database test...\n";

    // Test source code with comprehensive SQL operations
    constexpr const char *source = R"TRX(
        // Test table creation with various data types
        ROUTINE test_table_creation() {
            EXEC SQL DROP TABLE IF EXISTS test_types;
            EXEC SQL CREATE TABLE test_types (
                id INTEGER PRIMARY KEY,
                name VARCHAR(100) NOT NULL,
                age INTEGER,
                salary DECIMAL(10,2),
                active BOOLEAN DEFAULT TRUE,
                created_date DATE,
                data BLOB
            );

            EXEC SQL DROP TABLE IF EXISTS departments;
            EXEC SQL CREATE TABLE IF NOT EXISTS departments (
                dept_id INTEGER PRIMARY KEY,
                dept_name VARCHAR(50) UNIQUE NOT NULL,
                budget DECIMAL(12,2),
                location VARCHAR(100)
            );

            EXEC SQL DROP TABLE IF EXISTS employees;
            EXEC SQL CREATE TABLE employees (
                emp_id INTEGER PRIMARY KEY,
                first_name VARCHAR(50) NOT NULL,
                last_name VARCHAR(50) NOT NULL,
                dept_id INTEGER,
                salary DECIMAL(10,2),
                hire_date DATE,
                FOREIGN KEY (dept_id) REFERENCES departments(dept_id)
            );

            trace("Tables created successfully");
        }

        // Test data insertion
        ROUTINE test_data_insertion() {
            // Insert departments
            EXEC SQL INSERT INTO departments (dept_id, dept_name, budget, location) VALUES
                (1, 'Engineering', 500000.00, 'Building A'),
                (2, 'Sales', 300000.00, 'Building B'),
                (3, 'Marketing', 200000.00, 'Building C');

            // Insert employees
            EXEC SQL INSERT INTO employees (emp_id, first_name, last_name, dept_id, salary, hire_date) VALUES
                (1, 'John', 'Doe', 1, 75000.00, '2023-01-15'),
                (2, 'Jane', 'Smith', 1, 80000.00, '2023-02-01'),
                (3, 'Bob', 'Johnson', 2, 60000.00, '2023-03-10'),
                (4, 'Alice', 'Williams', 3, 55000.00, '2023-04-05'),
                (5, 'Charlie', 'Brown', 1, 70000.00, '2023-05-20');

            // Insert test data with various types
            EXEC SQL INSERT INTO test_types (id, name, age, salary, active, created_date)
                VALUES (1, 'Test User', 30, 50000.50, TRUE, '2024-01-01');

            trace("Data inserted successfully");
        }

        // Test SELECT operations
        ROUTINE test_select_operations() : JSON {
            var results JSON := {};

            // Simple SELECT
            var count INTEGER;
            EXEC SQL DECLARE count_cursor CURSOR FOR SELECT COUNT(*) FROM employees;
            EXEC SQL OPEN count_cursor;
            EXEC SQL FETCH count_cursor INTO :count;
            EXEC SQL CLOSE count_cursor;

            // SELECT with WHERE
            var eng_count INTEGER;
            EXEC SQL DECLARE eng_cursor CURSOR FOR SELECT COUNT(*) FROM employees WHERE dept_id = 1;
            EXEC SQL OPEN eng_cursor;
            EXEC SQL FETCH eng_cursor INTO :eng_count;
            EXEC SQL CLOSE eng_cursor;

            // SELECT with ORDER BY
            var top_salary DECIMAL;
            EXEC SQL DECLARE salary_cursor CURSOR FOR
                SELECT salary FROM employees ORDER BY salary DESC;
            EXEC SQL OPEN salary_cursor;
            EXEC SQL FETCH salary_cursor INTO :top_salary;
            EXEC SQL CLOSE salary_cursor;

            // SELECT with aggregate functions
            var avg_salary DECIMAL;
            var max_salary DECIMAL;
            var min_salary DECIMAL;

            EXEC SQL DECLARE agg_cursor CURSOR FOR
                SELECT AVG(salary), MAX(salary), MIN(salary) FROM employees;
            EXEC SQL OPEN agg_cursor;
            EXEC SQL FETCH agg_cursor INTO :avg_salary, :max_salary, :min_salary;
            EXEC SQL CLOSE agg_cursor;

            results := {
                "total_employees": count,
                "engineering_employees": eng_count,
                "top_salary": top_salary,
                "salary_stats": {
                    "average": avg_salary,
                    "maximum": max_salary,
                    "minimum": min_salary
                }
            };

            RETURN results;
        }

        // Test UPDATE operations
        ROUTINE test_update_operations() {
            // Update single record
            EXEC SQL UPDATE employees SET salary = salary * 1.1 WHERE emp_id = 1;

            // Update multiple records
            EXEC SQL UPDATE employees SET salary = salary * 1.05 WHERE dept_id = 2;

            trace("Update operations completed");
        }

        // Test DELETE operations
        ROUTINE test_delete_operations() {
            // Delete specific record
            EXEC SQL DELETE FROM test_types WHERE id = 1;

            // Delete with condition
            EXEC SQL DELETE FROM employees WHERE salary < 60000;

            trace("Delete operations completed");
        }

        // Test cursor operations with different fetch patterns
        ROUTINE test_cursor_operations() : JSON {
            var results JSON := [];

            // Cursor with ORDER BY and LIMIT equivalent
            EXEC SQL DECLARE ordered_cursor CURSOR FOR
                SELECT first_name, last_name FROM employees
                ORDER BY salary DESC;

            EXEC SQL OPEN ordered_cursor;

            // Initial fetch
            var first_name CHAR(50);
            var last_name CHAR(50);
            EXEC SQL FETCH ordered_cursor INTO :first_name, :last_name;

            WHILE (sqlcode = 0) {
                var row JSON := {
                    "first_name": first_name,
                    "last_name": last_name
                };
                append(results, row);

                // Fetch next row
                EXEC SQL FETCH ordered_cursor INTO :first_name, :last_name;
            }

            EXEC SQL CLOSE ordered_cursor;

            RETURN results;
        }

        // Test error handling
        ROUTINE test_error_handling() {
            // Try to insert duplicate primary key (should fail)
            TRY {
                EXEC SQL INSERT INTO departments (dept_id, dept_name) VALUES (1, 'Duplicate');
            } CATCH (ex) {
                trace("Expected error caught: " + ex.message);
            }

            // Try to select from non-existent table
            TRY {
                EXEC SQL DECLARE error_cursor CURSOR FOR SELECT * FROM nonexistent_table;
                EXEC SQL OPEN error_cursor;
            } CATCH (ex) {
                trace("Expected error caught for non-existent table: " + ex.message);
            }
        }

        // Main test procedure that runs all tests
        ROUTINE run_all_database_tests() : JSON {
            test_table_creation();
            test_data_insertion();

            var select_results JSON := test_select_operations();
            test_update_operations();
            test_delete_operations();
            
            var cursor_results JSON := test_cursor_operations();
            test_error_handling();

            // Final verification
            var final_count INTEGER;
            EXEC SQL DECLARE final_cursor CURSOR FOR SELECT COUNT(*) FROM employees;
            EXEC SQL OPEN final_cursor;
            EXEC SQL FETCH final_cursor INTO :final_count;
            EXEC SQL CLOSE final_cursor;

            RETURN {
                "status": "completed",
                "select_results": select_results,
                "cursor_results": cursor_results,
                "final_employee_count": final_count,
                "message": "All database operations completed successfully"
            };
        }
    )TRX";

    std::cout << "Parsing comprehensive database test source...\n";

    trx::parsing::ParserDriver driver;
    if (!driver.parseString(source, "comprehensive_db_test.trx")) {
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

    std::cout << "Executing comprehensive database tests...\n";

    // Run the main test procedure
    const auto outputOpt = interpreter.execute("run_all_database_tests", input);
    if (!outputOpt) {
        std::cerr << "run_all_database_tests procedure failed to execute\n";
        return false;
    }

    const auto& result = *outputOpt;
    if (!std::holds_alternative<trx::runtime::JsonValue::Object>(result.data)) {
        std::cerr << "run_all_database_tests did not return JSON object\n";
        return false;
    }

    const auto& obj = std::get<trx::runtime::JsonValue::Object>(result.data);
    auto statusIt = obj.find("status");
    if (statusIt == obj.end() || !std::holds_alternative<std::string>(statusIt->second.data)) {
        std::cerr << "Test result missing status field\n";
        return false;
    }

    const auto& status = std::get<std::string>(statusIt->second.data);
    if (status != "completed") {
        std::cerr << "Test did not complete successfully, status: " << status << "\n";
        return false;
    }

    std::cout << "Comprehensive database tests completed successfully!\\n";
    std::cout << "Test results: " << result << "\\n";
    
        std::cout << backend.name << " tests passed.\\n";
    }

    std::cout << "All comprehensive database tests passed across all backends!\\n";
    return true;
}

} // namespace trx::test

int main() {
    if (!trx::test::runComprehensiveDatabaseTest()) {
        std::cerr << "Comprehensive database tests failed.\n";
        return 1;
    }

    std::cout << "All comprehensive database tests passed!\n";
    return 0;
}