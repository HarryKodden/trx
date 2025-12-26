#include "Server.h"
#include "trx/parsing/ParserDriver.h"
#include "trx/runtime/Interpreter.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

namespace {

void printUsage() {
    std::cerr << "Usage:\n";
    std::cerr << "  trx_compiler <source.trx>\n";
    std::cerr << "  trx_compiler [--procedure <name>] [--db-type <type>] [--db-connection <conn>] <source.trx>\n";
    std::cerr << "  trx_compiler serve [--port <port>] [--threads <count>] [--procedure <name>] [--db-type <type>] [--db-connection <conn>] [source paths...]\n";
    std::cerr << "  trx_compiler list <source.trx>\n";
    std::cerr << "    If no source paths are provided for serve, all .trx files in the current directory are used.\n";
    std::cerr << "\nDatabase options:\n";
    std::cerr << "  --db-type <type>        Database type: sqlite, postgresql, odbc (default: sqlite)\n";
    std::cerr << "  --db-connection <conn>  Database connection string/path (default: :memory: for sqlite)\n";
    std::cerr << "\nServer options:\n";
    std::cerr << "  --port <port>           Port to listen on (default: 8080)\n";
    std::cerr << "  --threads <count>       Number of worker threads (default: hardware concurrency)\n";
}

void printDiagnostic(const trx::diagnostics::Diagnostic &diagnostic, const std::filesystem::path &filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << diagnostic.message << "\n";
        return;
    }

    std::string line;
    std::size_t currentLine = 1;
    while (std::getline(file, line)) {
        if (currentLine == diagnostic.location.line) {
            std::cerr << filePath.string() << ":" << diagnostic.location.line << ":" << diagnostic.location.column << ": " << diagnostic.message << "\n";
            std::cerr << line << "\n";
            std::string caret(diagnostic.location.column - 1, ' ');
            caret += "^";
            std::cerr << caret << "\n";
            return;
        }
        ++currentLine;
    }
    // If line not found, just print the message
    std::cerr << diagnostic.message << "\n";
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    bool serveMode = false;
    bool listMode = false;
    trx::cli::ServeOptions serveOptions;
    std::vector<std::filesystem::path> sourcePaths;
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
        if (argument == "list") {
            listMode = true;
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
        if ((argument == "--threads" || argument == "-T") && index + 1 < argc) {
            try {
                serveOptions.threadCount = std::stoul(argv[++index]);
            } catch (const std::exception &) {
                std::cerr << "Invalid thread count value\n";
                return 1;
            }
            if (serveOptions.threadCount == 0) {
                std::cerr << "Thread count must be at least 1\n";
                return 1;
            }
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
        if (argument.starts_with('-')) {
            std::cerr << "Unexpected argument: " << argument << "\n";
            return 1;
        }
        sourcePaths.push_back(std::filesystem::path{argument});
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

    if (listMode) {
        if (sourcePaths.empty()) {
            std::cerr << "Missing TRX source file for list\n";
            printUsage();
            return 1;
        }
        if (sourcePaths.size() > 1) {
            std::cerr << "Only one source file supported for list\n";
            return 1;
        }
        auto sourcePath = sourcePaths[0];

        trx::parsing::ParserDriver driver;
        if (!driver.parseFile(sourcePath)) {
            for (const auto &diagnostic : driver.diagnostics().messages()) {
                printDiagnostic(diagnostic, sourcePath);
            }
            return 1;
        }

        std::cout << "Procedures and functions in " << sourcePath.string() << ":\n";
        for (const auto &decl : driver.context().module().declarations) {
            if (const auto *proc = std::get_if<trx::ast::ProcedureDecl>(&decl)) {
                std::cout << "  " << proc->name.baseName << "\n";
            }
        }
        return 0;
    }

    if (serveMode) {
        if (sourcePaths.empty()) {
            sourcePaths.push_back(".");
        }
        serveOptions.dbConfig = dbConfig;
        return trx::cli::runServer(sourcePaths, serveOptions);
    }

    if (sourcePaths.empty()) {
        std::cerr << "Missing TRX source file\n";
        printUsage();
        return 1;
    }
    if (sourcePaths.size() > 1) {
        std::cerr << "Only one source file supported for compilation\n";
        return 1;
    }
    auto sourcePath = sourcePaths[0];

    trx::parsing::ParserDriver driver;
    if (!driver.parseFile(sourcePath)) {
        for (const auto &diagnostic : driver.diagnostics().messages()) {
            printDiagnostic(diagnostic, sourcePath);
        }
        return 1;
    }

    std::cout << "Parsed " << sourcePath.string() << " successfully\n";

    if (procedureToExecute) {
        auto dbDriver = trx::runtime::createDatabaseDriver(dbConfig);
        trx::runtime::Interpreter interpreter{driver.context().module(), std::move(dbDriver)};
        try {
            trx::runtime::JsonValue input = trx::runtime::JsonValue::object(); // For now, empty input
            auto result = interpreter.execute(*procedureToExecute, input);
            std::cout << "Executed procedure '" << *procedureToExecute << "' successfully\n";
            if (result) {
                std::cout << "Result: " << *result << "\n";
            }
        } catch (const std::exception &e) {
            std::cerr << "Error executing procedure '" << *procedureToExecute << "': " << e.what() << "\n";
            return 1;
        }
    }

    return 0;
}
