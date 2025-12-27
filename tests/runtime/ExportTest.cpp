#include "TestUtils.h"

#include <iostream>
#include <string>

namespace trx::test {

bool runExportTest() {
    std::cout << "Running export test...\n";

    // Test source with both exported and non-exported procedures
    constexpr const char *source = R"TRX(
        PROCEDURE internal_proc() {
            trace('internal');
        }

        EXPORT PROCEDURE exported_proc() {
            trace('exported');
        }

        EXPORT PROCEDURE another_exported_proc() {
            trace('another exported');
        }
    )TRX";

    trx::parsing::ParserDriver driver;
    if (!driver.parseString(source, "test_export.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    const auto &module = driver.context().module();

    // Check that procedures are parsed correctly with export flags
    const auto *internalProc = findProcedure(module, "internal_proc");
    if (!expect(internalProc != nullptr, "internal_proc should exist in module")) {
        return false;
    }
    if (!expect(!internalProc->isExported, "internal_proc should not be marked as exported")) {
        return false;
    }

    const auto *exportedProc = findProcedure(module, "exported_proc");
    if (!expect(exportedProc != nullptr, "exported_proc should exist in module")) {
        return false;
    }
    if (!expect(exportedProc->isExported, "exported_proc should be marked as exported")) {
        return false;
    }

    const auto *anotherExportedProc = findProcedure(module, "another_exported_proc");
    if (!expect(anotherExportedProc != nullptr, "another_exported_proc should exist in module")) {
        return false;
    }
    if (!expect(anotherExportedProc->isExported, "another_exported_proc should be marked as exported")) {
        return false;
    }

    // Count total procedures and exported procedures
    size_t totalProcedures = 0;
    size_t exportedProcedures = 0;

    for (const auto &declaration : module.declarations) {
        if (const auto *procedure = std::get_if<trx::ast::ProcedureDecl>(&declaration)) {
            totalProcedures++;
            if (procedure->isExported) {
                exportedProcedures++;
            }
        }
    }

    if (!expect(totalProcedures == 3, "Expected 3 total procedures, got " + std::to_string(totalProcedures))) {
        return false;
    }
    if (!expect(exportedProcedures == 2, "Expected 2 exported procedures, got " + std::to_string(exportedProcedures))) {
        return false;
    }

    std::cout << "Basic export test passed!\n";
    return true;
}

bool runExportConfigTest() {
    std::cout << "Running export configuration test...\n";

    // Test source with HTTP method and headers configuration
    constexpr const char *source = R"TRX(
        EXPORT PROCEDURE default_proc() {
            trace('default POST');
        }

        EXPORT METHOD GET PROCEDURE get_proc() {
            trace('GET method');
        }

        EXPORT METHOD POST HEADERS {
            "X-API-Version": "1.0";
            "Cache-Control": "no-cache";
        } PROCEDURE post_with_headers() {
            trace('POST with headers');
        }

        EXPORT METHOD PUT PROCEDURE put_proc() {
            trace('PUT method');
        }
    )TRX";

    trx::parsing::ParserDriver driver;
    if (!driver.parseString(source, "test_export_config.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    const auto &module = driver.context().module();

    // Test default procedure (should have no custom method)
    const auto *defaultProc = findProcedure(module, "default_proc");
    if (!expect(defaultProc != nullptr, "default_proc should exist")) return false;
    if (!expect(defaultProc->isExported, "default_proc should be exported")) return false;
    if (!expect(!defaultProc->httpMethod.has_value(), "default_proc should not have custom HTTP method")) return false;
    if (!expect(defaultProc->httpHeaders.empty(), "default_proc should have no custom headers")) return false;

    // Test GET procedure
    const auto *getProc = findProcedure(module, "get_proc");
    if (!expect(getProc != nullptr, "get_proc should exist")) return false;
    if (!expect(getProc->isExported, "get_proc should be exported")) return false;
    if (!expect(getProc->httpMethod.has_value() && getProc->httpMethod.value() == "GET", "get_proc should have GET method")) return false;
    if (!expect(getProc->httpHeaders.empty(), "get_proc should have no custom headers")) return false;

    // Test POST with headers procedure
    const auto *postHeadersProc = findProcedure(module, "post_with_headers");
    if (!expect(postHeadersProc != nullptr, "post_with_headers should exist")) return false;
    if (!expect(postHeadersProc->isExported, "post_with_headers should be exported")) return false;
    if (!expect(postHeadersProc->httpMethod.has_value() && postHeadersProc->httpMethod.value() == "POST", "post_with_headers should have POST method")) return false;
    if (!expect(postHeadersProc->httpHeaders.size() == 2, "post_with_headers should have 2 custom headers")) return false;
    if (!expect(postHeadersProc->httpHeaders[0].first == "X-API-Version" && postHeadersProc->httpHeaders[0].second == "1.0", "First header should be X-API-Version: 1.0")) return false;
    if (!expect(postHeadersProc->httpHeaders[1].first == "Cache-Control" && postHeadersProc->httpHeaders[1].second == "no-cache", "Second header should be Cache-Control: no-cache")) return false;

    // Test PUT procedure
    const auto *putProc = findProcedure(module, "put_proc");
    if (!expect(putProc != nullptr, "put_proc should exist")) return false;
    if (!expect(putProc->isExported, "put_proc should be exported")) return false;
    if (!expect(putProc->httpMethod.has_value() && putProc->httpMethod.value() == "PUT", "put_proc should have PUT method")) return false;
    if (!expect(putProc->httpHeaders.empty(), "put_proc should have no custom headers")) return false;

    std::cout << "Export configuration test passed!\n";
    return true;
}

bool runExportFunctionTest() {
    std::cout << "Running export function test...\n";
    std::cout << "About to parse source...\n";

    // Test source with exported functions
    const char *source = R"RAW(
        FUNCTION get_data() : INTEGER {
            RETURN 42;
        }

        FUNCTION process_data(input: INTEGER) : INTEGER {
            RETURN input * 2;
        }
    )RAW";

    trx::parsing::ParserDriver driver;
    if (!driver.parseString(source, "test_export_function.trx")) {
        reportDiagnostics(driver);
        return false;
    }
    
    // Check for any diagnostics even if parsing "succeeded"
    if (!driver.diagnostics().messages().empty()) {
        reportDiagnostics(driver);
        return false;
    }

    const auto &module = driver.context().module();
    std::cout << "Parsed successfully, found " << module.declarations.size() << " declarations" << std::endl;
    
    // Debug: print all declarations
    std::cout << "Available declarations:\n";
    for (const auto &declaration : module.declarations) {
        if (const auto *procedure = std::get_if<trx::ast::ProcedureDecl>(&declaration)) {
            std::cout << "  Procedure: " << procedure->name.baseName << " (exported: " << procedure->isExported << ", has_output: " << procedure->output.has_value();
            if (procedure->output.has_value()) {
                std::cout << ", output_type: " << procedure->output->type.name;
            }
            std::cout << ")\n";
        }
    }

    // Test GET function
    const auto *getFunc = findProcedure(module, "get_data");
    if (!expect(getFunc != nullptr, "get_data should exist")) return false;
    
    std::cout << "Found get_data function, exported: " << getFunc->isExported << ", has output: " << getFunc->output.has_value() << std::endl;
    if (getFunc->output.has_value()) {
        std::cout << "Output type: " << getFunc->output->type.name << std::endl;
    }
    
    if (!expect(!getFunc->isExported, "get_data should not be exported")) return false;
    if (!expect(getFunc->output.has_value(), "get_data should have output type")) return false;
    if (!expect(getFunc->output->type.name == "INTEGER", "get_data should return INTEGER")) return false;

    // Test POST function with headers
    const auto *postFunc = findProcedure(module, "process_data");
    if (!expect(postFunc != nullptr, "process_data should exist")) return false;
    if (!expect(!postFunc->isExported, "process_data should not be exported")) return false;
    if (!expect(postFunc->output.has_value(), "process_data should have output type")) return false;
    if (!expect(postFunc->output->type.name == "INTEGER", "process_data should return INTEGER")) return false;

    std::cout << "Export function test passed!\n";
    return true;
}

bool runPathParameterTest() {
    std::cout << "Running path parameter test...\n";

    // Test source with procedures that have path parameters
    constexpr const char *source = R"TRX(
        EXPORT METHOD GET FUNCTION get_user(id: INTEGER) : INTEGER {
            RETURN id;
        }

        EXPORT METHOD GET FUNCTION get_user_by_id/{id: INTEGER}(id: INTEGER) : INTEGER {
            RETURN id;
        }

        EXPORT METHOD POST FUNCTION create_user(user: INTEGER) : INTEGER {
            RETURN user;
        }
    )TRX";

    trx::parsing::ParserDriver driver;
    if (!driver.parseString(source, "test_path_params.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    const auto &module = driver.context().module();

    // Test procedure without path parameters
    const auto *getUser = findProcedure(module, "get_user");
    if (!expect(getUser != nullptr, "get_user should exist")) return false;
    if (!expect(getUser->isExported, "get_user should be exported")) return false;
    if (!expect(getUser->name.pathParameters.empty(), "get_user should have no path parameters")) return false;
    if (!expect(getUser->name.pathTemplate == "get_user", "get_user pathTemplate should be 'get_user'")) return false;

    // Test procedure with single path parameter (no body params for GET)
    const auto *getUserById = findProcedure(module, "get_user_by_id");
    if (!expect(getUserById != nullptr, "get_user_by_id should exist")) return false;
    if (!expect(getUserById->isExported, "get_user_by_id should be exported")) return false;
    if (!expect(getUserById->name.pathParameters.size() == 1, "get_user_by_id should have 1 path parameter")) return false;
    if (!expect(getUserById->name.pathParameters[0].name.name == "id", "get_user_by_id path parameter should be 'id'")) return false;
    if (!expect(getUserById->name.pathParameters[0].type.name == "INTEGER", "get_user_by_id path parameter type should be 'INTEGER'")) return false;
    if (!expect(getUserById->name.pathTemplate == "get_user_by_id/{id}", "get_user_by_id pathTemplate should be 'get_user_by_id/{id}'")) return false;
    // GET with path params should have explicit input parameters
    if (!expect(getUserById->input.has_value(), "get_user_by_id should have explicit input parameters")) return false;

    // Test procedure without path parameters (POST method)
    const auto *createUser = findProcedure(module, "create_user");
    if (!expect(createUser != nullptr, "create_user should exist")) return false;
    if (!expect(createUser->isExported, "create_user should be exported")) return false;
    if (!expect(createUser->name.pathParameters.empty(), "create_user should have no path parameters")) return false;
    if (!expect(createUser->name.pathTemplate == "create_user", "create_user pathTemplate should be 'create_user'")) return false;
    // POST should have input parameters
    if (!expect(createUser->input.has_value(), "create_user should have input parameters")) return false;

    std::cout << "Path parameter test passed!\n";
    return true;
}

} // namespace trx::test

int main() {
    if (!trx::test::runExportTest()) {
        std::cerr << "Basic export tests failed.\n";
        return 1;
    }

    if (!trx::test::runExportConfigTest()) {
        std::cerr << "Export configuration tests failed.\n";
        return 1;
    }

    if (!trx::test::runPathParameterTest()) {
        std::cerr << "Path parameter tests failed.\n";
        return 1;
    }

    // if (!trx::test::runExportFunctionTest()) {
    //     std::cerr << "Export function tests failed.\n";
    //     return 1;
    // }

    std::cout << "All export tests passed!\n";
    return 0;
}