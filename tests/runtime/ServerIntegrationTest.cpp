#include "TestUtils.h"

#include <iostream>
#include <string>
#include <vector>
#include <variant>

#include "trx/parsing/ParserDriver.h"
#include "trx/ast/Nodes.h"

namespace trx::test {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

bool runServerIntegrationTest() {
    std::cout << "Running server integration test...\n";

    // For now, just test that we can parse the server test file
    // Full HTTP integration testing would require a more complex setup
    constexpr const char *source = R"TRX(
        EXPORT METHOD GET ROUTINE get_user/{id: INTEGER}() : INTEGER {
            RETURN id;
        }

        EXPORT METHOD GET ROUTINE get_user_by_name/{name: CHAR}() : CHAR {
            RETURN name;
        }

        EXPORT METHOD POST ROUTINE create_user(user: JSON) : JSON {
            RETURN user;
        }
    )TRX";

    trx::parsing::ParserDriver driver;
    if (!driver.parseString(source, "server_integration_test.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    const auto &module = driver.context().module();

    // Test that procedures with path parameters are parsed correctly
    const auto *getUser = findProcedure(module, "get_user");
    if (!expect(getUser != nullptr, std::string("get_user should exist"))) return false;
    if (!expect(getUser->isExported, std::string("get_user should be exported"))) return false;
    if (!expect(getUser->name.pathParameters.size() == 1, std::string("get_user should have 1 path parameter"))) return false;
    if (!expect(getUser->name.pathParameters[0].name.name == "id", std::string("get_user path parameter should be 'id'"))) return false;
    if (!expect(getUser->name.pathParameters[0].type.name == "INTEGER", std::string("get_user path parameter type should be 'INTEGER'"))) return false;

    const auto *getUserByName = findProcedure(module, "get_user_by_name");
    if (!expect(getUserByName != nullptr, std::string("get_user_by_name should exist"))) return false;
    if (!expect(getUserByName->isExported, std::string("get_user_by_name should be exported"))) return false;
    if (!expect(getUserByName->name.pathParameters.size() == 1, std::string("get_user_by_name should have 1 path parameter"))) return false;
    if (!expect(getUserByName->name.pathParameters[0].name.name == "name", std::string("get_user_by_name path parameter should be 'name'"))) return false;
    if (!expect(getUserByName->name.pathParameters[0].type.name == "_CHAR", std::string("get_user_by_name path parameter type should be '_CHAR'"))) return false;

    const auto *createUser = findProcedure(module, "create_user");
    if (!expect(createUser != nullptr, std::string("create_user should exist"))) return false;
    if (!expect(createUser->isExported, std::string("create_user should be exported"))) return false;
    if (!expect(createUser->name.pathParameters.empty(), std::string("create_user should have no path parameters"))) return false;

    std::cout << "Server integration test passed (parsing only)!\n";
    std::cout << "Note: Full HTTP integration testing would require starting the server in a separate process.\n";
    return true;
}

} // namespace trx::test

int main() {
    if (!trx::test::runServerIntegrationTest()) {
        std::cerr << "Server integration test failed.\n";
        return 1;
    }

    std::cout << "All server integration tests passed!\n";
    return 0;
}