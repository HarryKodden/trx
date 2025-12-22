#include "SwaggerServer.h"
#include "trx/parsing/ParserDriver.h"
#include "trx/runtime/Interpreter.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace {

void printUsage() {
    std::cerr << "Usage:\n";
    std::cerr << "  trx_compiler <source.trx>\n";
    std::cerr << "  trx_compiler [--procedure <name>] [--db-type <type>] [--db-connection <conn>] <source.trx>\n";
    std::cerr << "  trx_compiler serve [--port <port>] [--procedure <name>] [--db-type <type>] [--db-connection <conn>] <source path>\n";
    std::cerr << "\nDatabase options:\n";
    std::cerr << "  --db-type <type>        Database type: sqlite, postgresql, odbc (default: sqlite)\n";
    std::cerr << "  --db-connection <conn>  Database connection string/path (default: :memory: for sqlite)\n";
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    bool serveMode = false;
    trx::cli::ServeOptions serveOptions;
    std::optional<std::filesystem::path> sourcePath;
    std::optional<std::string> procedureToExecute;
    trx::runtime::DatabaseConfig dbConfig;
    dbConfig.type = trx::runtime::DatabaseType::SQLITE;
    dbConfig.databasePath = ":memory:";

    for (int index = 1; index < argc; ++index) {
        const std::string argument{argv[index]};
        if (argument == "-h" || argument == "--help") {
            printUsage();
            return 0;
        }
        if (argument == "serve" || argument == "--serve") {
            serveMode = true;
            continue;
        }
        if ((argument == "--port" || argument == "-p") && index + 1 < argc) {
            try {
                serveOptions.port = std::stoi(argv[++index]);
            } catch (const std::exception &) {
                std::cerr << "Invalid port value\n";
                return 1;
            }
            if (serveOptions.port <= 0 || serveOptions.port > 65535) {
                std::cerr << "Port must be between 1 and 65535\n";
                return 1;
            }
            continue;
        }
        if ((argument == "--procedure" || argument == "-r") && index + 1 < argc) {
            procedureToExecute = std::string{argv[++index]};
            continue;
        }
        if ((argument == "--db-type" || argument == "-t") && index + 1 < argc) {
            std::string dbType = argv[++index];
            if (dbType == "sqlite") {
                dbConfig.type = trx::runtime::DatabaseType::SQLITE;
            } else if (dbType == "postgresql") {
                dbConfig.type = trx::runtime::DatabaseType::POSTGRESQL;
            } else if (dbType == "odbc") {
                dbConfig.type = trx::runtime::DatabaseType::ODBC;
            } else {
                std::cerr << "Unknown database type: " << dbType << "\n";
                return 1;
            }
            continue;
        }
        if ((argument == "--db-connection" || argument == "-c") && index + 1 < argc) {
            std::string connStr = argv[++index];
            if (dbConfig.type == trx::runtime::DatabaseType::SQLITE) {
                dbConfig.databasePath = connStr;
            } else if (dbConfig.type == trx::runtime::DatabaseType::ODBC) {
                dbConfig.connectionString = connStr;
            } else {
                dbConfig.connectionString = connStr;
            }
            continue;
        }
        if (!sourcePath) {
            sourcePath = std::filesystem::path{argument};
            continue;
        }

        std::cerr << "Unexpected argument: " << argument << "\n";
        return 1;
    }

    // Check environment variables for database configuration if not set via command line
    if (const char* dbTypeEnv = std::getenv("DATABASE_TYPE")) {
        std::string dbType = dbTypeEnv;
        if (dbType == "ODBC") {
            dbConfig.type = trx::runtime::DatabaseType::ODBC;
        } else if (dbType == "POSTGRESQL") {
            dbConfig.type = trx::runtime::DatabaseType::POSTGRESQL;
        } else if (dbType == "SQLITE") {
            dbConfig.type = trx::runtime::DatabaseType::SQLITE;
        }
    }
    if (const char* dbConnEnv = std::getenv("DATABASE_CONNECTION_STRING")) {
        if (dbConfig.type == trx::runtime::DatabaseType::ODBC) {
            dbConfig.connectionString = dbConnEnv;
        } else if (dbConfig.type == trx::runtime::DatabaseType::POSTGRESQL) {
            dbConfig.connectionString = dbConnEnv;
        } else {
            dbConfig.databasePath = dbConnEnv;
        }
    }

    if (!sourcePath) {
        std::cerr << "Missing TRX source file\n";
        printUsage();
        return 1;
    }

    if (serveMode) {
        serveOptions.dbConfig = dbConfig;
        return trx::cli::runSwaggerServer(*sourcePath, serveOptions);
    }

    trx::parsing::ParserDriver driver;
    if (!driver.parseFile(*sourcePath)) {
        for (const auto &diagnostic : driver.diagnostics().messages()) {
            std::cerr << diagnostic.message << "\n";
        }
        return 1;
    }

    std::cout << "Parsed " << sourcePath->string() << " successfully\n";

    if (procedureToExecute) {
        auto dbDriver = trx::runtime::createDatabaseDriver(dbConfig);
        trx::runtime::Interpreter interpreter{driver.context().module(), std::move(dbDriver)};
        try {
            trx::runtime::JsonValue input = trx::runtime::JsonValue::object(); // For now, empty input
            trx::runtime::JsonValue result = interpreter.execute(*procedureToExecute, input);
            std::cout << "Executed procedure '" << *procedureToExecute << "' successfully\n";
            std::cout << "Result: " << result << "\n";
        } catch (const std::exception &e) {
            std::cerr << "Error executing procedure '" << *procedureToExecute << "': " << e.what() << "\n";
            return 1;
        }
    }

    return 0;
}
