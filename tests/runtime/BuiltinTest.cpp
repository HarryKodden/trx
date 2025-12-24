#include "TestUtils.h"

#include <iostream>

namespace trx::test {

bool runBuiltinTest() {
    std::cout << "Running builtin test...\n";
    constexpr const char *source = R"TRX(
        FUNCTION create_nested_json() : JSON {
            var result JSON := {
                "name": "John Doe",
                "age": 30,
                "active": true,
                "scores": [85, 92, 78, 96],
                "address": {
                    "street": "123 Main St",
                    "city": "Anytown",
                    "coordinates": {
                        "lat": 40.7128,
                        "lon": -74.0060
                    }
                },
                "tags": ["developer", "json", "test"]
            };
            RETURN result;
        }

        PROCEDURE test_builtins() {
            // Test SqlCode - should be 0 initially
            var sqlcode_val DECIMAL := sqlcode;
            trace('sqlcode=' + sqlcode_val);

            // Test Date - should return current date as string
            var date_val CHAR(20) := date;
            trace('date=' + date_val);

            // Test Time - should return current time as string
            var time_val CHAR(20) := time;
            trace('time=' + time_val);

            // Test TimeStamp - should return timestamp
            var stamp_val DECIMAL := timestamp;
            trace('timestamp=' + stamp_val);

            // Test Week - should return week number
            var week_val INTEGER := week;
            trace('week=' + week_val);

            // Test WeekDay - should return day of week
            var weekday_val INTEGER := weekday;
            trace('weekday=' + weekday_val);

            // Test JSON as generic type
            var json_val JSON;
            json_val := "hello world";  // Assign string
            trace('json string=' + json_val);
            
            json_val := 42;  // Assign number
            trace('json number=' + json_val);
            
            json_val := true;  // Assign boolean
            trace('json boolean=' + json_val);

            // Test function returning JSON with nested elements
            var nested_json JSON := create_nested_json();
            trace('nested json created successfully');

            output := nested_json; // Return the nested JSON as output
        }

        PROCEDURE test_http_request() {
            // Test HTTP request functionality
            var request_config JSON := {
                "method": "GET",
                "url": "https://api.example.com/users",
                "headers": {
                    "Authorization": "Bearer test-token",
                    "Content-Type": "application/json"
                },
                "timeout": 30
            };

            var response JSON := http_request(request_config);
            trace('HTTP request completed with status: ' + response.status);
            trace('Response headers: ' + response.headers);
            trace('Response body: ' + response.body);

            output := response;
        }
    )TRX";

    std::cout << "Source code to parse:\n" << source << "\n";

    trx::parsing::ParserDriver driver;
    std::cout << "Parsing source...\n";
    if (!driver.parseString(source, "builtin_test.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    std::cout << "Parsing successful. Executing procedure...\n";
    trx::runtime::Interpreter interpreter(driver.context().module(), nullptr); // Use default SQLite driver

    trx::runtime::JsonValue input{trx::runtime::JsonValue::Object{}};

    std::cout << "Executing test_builtins...\n";
    const auto outputOpt1 = interpreter.execute("test_builtins", input);
    if (!outputOpt1) {
        std::cerr << "test_builtins procedure did not return a value\n";
        return false;
    }
    const auto &output1 = *outputOpt1;

    // Check that we got a JSON object with nested elements
    if (!output1.isObject()) {
        std::cerr << "test_builtins output is not a JSON object\n";
        return false;
    }
    
    const auto& result = output1.asObject();
    if (result.find("name") == result.end() || 
        !std::holds_alternative<std::string>(result.at("name").data) ||
        std::get<std::string>(result.at("name").data) != "John Doe") {
        std::cerr << "Name field is incorrect\n";
        return false;
    }

    std::cout << "Executing test_http_request...\n";
    const auto outputOpt2 = interpreter.execute("test_http_request", input);
    if (!outputOpt2) {
        std::cerr << "test_http_request procedure did not return a value\n";
        return false;
    }
    const auto &output2 = *outputOpt2;

    // Check that we got a JSON object with HTTP response structure
    if (!output2.isObject()) {
        std::cerr << "test_http_request output is not a JSON object\n";
        return false;
    }
    
    const auto& response = output2.asObject();
    if (response.find("status") == response.end() || 
        !std::holds_alternative<double>(response.at("status").data) ||
        std::get<double>(response.at("status").data) != 200.0) {
        std::cerr << "HTTP response status is incorrect\n";
        return false;
    }

    if (response.find("headers") == response.end() || !response.at("headers").isObject()) {
        std::cerr << "HTTP response headers are missing or incorrect\n";
        return false;
    }

    if (response.find("body") == response.end() || !response.at("body").isObject()) {
        std::cerr << "HTTP response body is missing or incorrect\n";
        return false;
    }

    std::cout << "Builtin test passed.\n";
    return true;
}

} // namespace trx::test

int main() {
    if (!trx::test::runBuiltinTest()) {
        std::cerr << "Builtin tests failed.\n";
        return 1;
    }

    std::cout << "All builtin tests passed!\n";
    return 0;
}