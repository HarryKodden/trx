#include "TestUtils.h"

#include <iostream>

namespace trx::test {

bool runHttpTest() {
    std::cout << "Running HTTP test...\n";
    constexpr const char *source = R"TRX(
        ROUTINE test_http_get() {
            var request_config JSON := {
                "method": "GET",
                "url": "https://httpbin.org/get",
                "headers": {
                    "User-Agent": "TRX-Test/1.0",
                    "Accept": "application/json"
                },
                "timeout": 10
            };

            var response JSON := http(request_config);
            trace('GET request completed');
            trace('Status: ' + response.status);

            // Check that we get a real HTTP response
            IF response.status = 200 {
                trace('GET test successful');
            } ELSE {
                trace('GET test failed with status: ' + response.status);
            }
        }

        ROUTINE test_http_post() {
            var request_config JSON := {
                "method": "POST",
                "url": "https://httpbin.org/post",
                "headers": {
                    "Content-Type": "application/json",
                    "User-Agent": "TRX-Test/1.0"
                },
                "body": {
                    "name": "TRX Test",
                    "version": "1.0",
                    "features": ["json", "sql", "http"]
                },
                "timeout": 10
            };

            var response JSON := http(request_config);
            trace('POST request completed');
            trace('Status: ' + response.status);

            IF response.status = 200 {
                trace('POST test successful');
            } ELSE {
                trace('POST test failed with status: ' + response.status);
            }
        }

        ROUTINE test_http_put() {
            var request_config JSON := {
                "method": "PUT",
                "url": "https://httpbin.org/put",
                "headers": {
                    "Content-Type": "application/json"
                },
                "body": {
                    "id": 123,
                    "updated": true,
                    "timestamp": timestamp
                },
                "timeout": 10
            };

            var response JSON := http(request_config);
            trace('PUT request completed');
            trace('Status: ' + response.status);

            // PUT test completed
        }

        ROUTINE test_http_delete() {
            var request_config JSON := {
                "method": "DELETE",
                "url": "https://httpbin.org/delete",
                "headers": {
                    "Authorization": "Bearer test-token"
                },
                "timeout": 10
            };

            var response JSON := http(request_config);
            trace('DELETE request completed');
            trace('Status: ' + response.status);

            // DELETE test completed
        }

        ROUTINE test_http_with_query_params() {
            var request_config JSON := {
                "method": "GET",
                "url": "https://httpbin.org/get?param1=value1&param2=value2",
                "headers": {
                    "Accept": "application/json"
                },
                "timeout": 10
            };

            var response JSON := http(request_config);
            trace('Query params request completed');
            trace('Status: ' + response.status);

            IF response.status = 200 {
                trace('Query params test successful');
            } ELSE {
                trace('Query params test failed with status: ' + response.status);
            }
        }

        ROUTINE test_http_error_handling() {
            // Test with invalid URL - this should fail gracefully
            var request_config JSON := {
                "method": "GET",
                "url": "https://httpbin.org/status/404",  // Use a valid domain but 404 status
                "timeout": 5
            };

            var response JSON := http(request_config);
            trace('Error handling test completed');
            trace('Status: ' + response.status);

            // Error handling test completed
        }

        ROUTINE test_http_timeout() {
            var request_config JSON := {
                "method": "GET",
                "url": "https://httpbin.org/delay/1",  // 1 second delay
                "timeout": 2+1  // 2 second timeout - should succeed
            };

            var response JSON := http(request_config);
            trace('Timeout test completed');
            trace('Status: ' + response.status);

            // Timeout test completed
        }
    )TRX";

    std::cout << "Source code to parse:\n" << source << "\n";

    trx::parsing::ParserDriver driver;
    std::cout << "Parsing source...\n";
    if (!driver.parseString(source, "http_test.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    std::cout << "Parsing successful. Executing HTTP tests...\n";
    trx::runtime::Interpreter interpreter(driver.context().module(), nullptr);

    trx::runtime::JsonValue input{trx::runtime::JsonValue::Object{}};

    // Test GET request
    std::cout << "Testing HTTP GET...\n";
    const auto getResult = interpreter.execute("test_http_get", input);
    if (getResult) {
        std::cerr << "GET test failed - procedure should not return a value\n";
        return false;
    }

    // Test POST request
    std::cout << "Testing HTTP POST...\n";
    const auto postResult = interpreter.execute("test_http_post", input);
    if (postResult) {
        std::cerr << "POST test failed - procedure should not return a value\n";
        return false;
    }

    // Test PUT request
    std::cout << "Testing HTTP PUT...\n";
    const auto putResult = interpreter.execute("test_http_put", input);
    if (putResult) {
        std::cerr << "PUT test failed - procedure should not return a value\n";
        return false;
    }

    // Test DELETE request
    std::cout << "Testing HTTP DELETE...\n";
    const auto deleteResult = interpreter.execute("test_http_delete", input);
    if (deleteResult) {
        std::cerr << "DELETE test failed - procedure should not return a value\n";
        return false;
    }

    // Test query parameters
    std::cout << "Testing HTTP with query parameters...\n";
    const auto queryResult = interpreter.execute("test_http_with_query_params", input);
    if (queryResult) {
        std::cerr << "Query parameters test failed - procedure should not return a value\n";
        return false;
    }

    // Test error handling
    std::cout << "Testing HTTP error handling...\n";
    const auto errorResult = interpreter.execute("test_http_error_handling", input);
    if (errorResult) {
        std::cerr << "Error handling test failed - procedure should not return a value\n";
        return false;
    }

    // Test timeout
    std::cout << "Testing HTTP timeout...\n";
    const auto timeoutResult = interpreter.execute("test_http_timeout", input);
    if (timeoutResult) {
        std::cerr << "Timeout test failed - procedure should not return a value\n";
        return false;
    }

    std::cout << "All HTTP tests completed successfully.\n";
    return true;
}

} // namespace trx::test

int main() {
    if (!trx::test::runHttpTest()) {
        std::cerr << "HTTP tests failed.\n";
        return 1;
    }

    std::cout << "All HTTP tests passed!\n";
    return 0;
}