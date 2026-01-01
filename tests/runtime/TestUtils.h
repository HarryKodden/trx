#pragma once

#include "trx/ast/Nodes.h"
#include "trx/ast/Statements.h"
#include "trx/parsing/ParserDriver.h"
#include "trx/runtime/Interpreter.h"
#include "trx/runtime/TrxException.h"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace trx::test {

struct DatabaseBackend {
    std::string name;
    trx::runtime::DatabaseConfig config;
};

inline std::vector<DatabaseBackend> getTestDatabaseBackends() {
    std::vector<DatabaseBackend> backends;
    
    // Check environment variable for backend selection
    const char* envBackends = std::getenv("TEST_DB_BACKENDS");
    std::string backendsStr = envBackends ? envBackends : "sqlite";
    
    // Always include SQLite as it requires no external setup
    if (backendsStr.find("sqlite") != std::string::npos || backendsStr.find("all") != std::string::npos) {
        DatabaseBackend sqlite;
        sqlite.name = "SQLite";
        sqlite.config.type = trx::runtime::DatabaseType::SQLITE;
        sqlite.config.databasePath = ":memory:";
        backends.push_back(sqlite);
    }
    
    // PostgreSQL - if enabled and connection info available
    if (backendsStr.find("postgresql") != std::string::npos || backendsStr.find("all") != std::string::npos) {
        const char* pgHost = std::getenv("POSTGRES_HOST");
        const char* pgPort = std::getenv("POSTGRES_PORT");
        const char* pgDb = std::getenv("POSTGRES_DB");
        const char* pgUser = std::getenv("POSTGRES_USER");
        const char* pgPass = std::getenv("POSTGRES_PASSWORD");
        
        if (pgHost || pgDb) {
            DatabaseBackend postgres;
            postgres.name = "PostgreSQL";
            postgres.config.type = trx::runtime::DatabaseType::POSTGRESQL;
            postgres.config.host = pgHost ? pgHost : "localhost";
            postgres.config.port = pgPort ? pgPort : "5432";
            postgres.config.connectionString = std::string("host=") + postgres.config.host +
                                              " port=" + postgres.config.port +
                                              " dbname=" + (pgDb ? pgDb : "trx") +
                                              " user=" + (pgUser ? pgUser : "trx") +
                                              " password=" + (pgPass ? pgPass : "password");
            backends.push_back(postgres);
        }
    }
    
    // ODBC - if enabled and DSN available
    if (backendsStr.find("odbc") != std::string::npos || backendsStr.find("all") != std::string::npos) {
        const char* odbcDsn = std::getenv("ODBC_CONNECTION_STRING");
        if (odbcDsn) {
            DatabaseBackend odbc;
            odbc.name = "ODBC";
            odbc.config.type = trx::runtime::DatabaseType::ODBC;
            odbc.config.connectionString = odbcDsn;
            backends.push_back(odbc);
        }
    }
    
    return backends;
}

inline std::unique_ptr<trx::runtime::DatabaseDriver> createTestDatabaseDriver(const DatabaseBackend& backend) {
    return trx::runtime::createDatabaseDriver(backend.config);
}

void reportDiagnostics(const trx::parsing::ParserDriver &driver) {
    std::cerr << "Parsing failed with " << driver.diagnostics().messages().size() << " diagnostic messages:\n";
    for (const auto &diagnostic : driver.diagnostics().messages()) {
        std::cerr << "  - " << diagnostic.message << " at " << diagnostic.location.file << ":" << diagnostic.location.line << ":" << diagnostic.location.column << "\n";
    }
}

bool expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

const trx::ast::ProcedureDecl *findProcedure(const trx::ast::Module &module, std::string_view name) {
    for (const auto &declaration : module.declarations) {
        if (const auto *procedure = std::get_if<trx::ast::ProcedureDecl>(&declaration)) {
            if (procedure->name.baseName == name) {
                return procedure;
            }
        }
    }
    return nullptr;
}

const trx::ast::RecordDecl *findRecord(const trx::ast::Module &module, std::string_view name) {
    for (const auto &declaration : module.declarations) {
        if (const auto *record = std::get_if<trx::ast::RecordDecl>(&declaration)) {
            if (record->name.name == name) {
                return record;
            }
        }
    }
    return nullptr;
}

bool expectVariablePath(const trx::ast::VariableExpression &var, std::initializer_list<std::string_view> expectedPath) {
    if (var.path.size() != expectedPath.size()) {
        std::cerr << "Variable path size mismatch: expected " << expectedPath.size() << ", got " << var.path.size() << "\n";
        return false;
    }

    std::size_t index = 0;
    for (const auto &segment : expectedPath) {
        if (var.path[index].identifier != segment) {
            std::cerr << "Variable path segment " << index << " mismatch: expected '" << segment << "', got '" << var.path[index].identifier << "'\n";
            return false;
        }
        ++index;
    }

    return true;
}

} // namespace trx::test