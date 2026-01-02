#pragma once

#include "trx/runtime/DatabaseDriver.h"

#include <sql.h>
#include <sqlext.h>
#include <memory>
#include <string>
#include <unordered_map>

namespace trx::runtime {

/**
 * ODBC implementation of the DatabaseDriver interface.
 * Note: This is a skeleton implementation. In a real scenario, you would need
 * to install and link against unixODBC or iODBC libraries.
 */
class ODBCDriver : public DatabaseDriver {
public:
    explicit ODBCDriver(const DatabaseConfig& config);
    ~ODBCDriver() override;

    void initialize() override;
    void executeSql(const std::string& sql, const std::vector<SqlParameter>& params = {}) override;
    std::vector<std::vector<SqlValue>> querySql(const std::string& sql, const std::vector<SqlParameter>& params = {}) override;
    void openCursor(const std::string& name, const std::string& sql, const std::vector<SqlParameter>& params = {}) override;
    void openDeclaredCursor(const std::string& name) override;
    void openDeclaredCursorWithParams(const std::string& name, const std::vector<SqlParameter>& params = {}) override;
    bool cursorNext(const std::string& name) override;
    std::vector<SqlValue> cursorGetRow(const std::string& name) override;
    void closeCursor(const std::string& name) override;
    void createOrMigrateTable(const std::string& tableName, const std::vector<TableColumn>& columns) override;
    std::vector<TableColumn> getTableSchema(const std::string& tableName) override;
    void beginTransaction() override;
    void commitTransaction() override;
    void rollbackTransaction() override;
    bool isInTransaction() override;

private:
    DatabaseConfig config_;
    SQLHENV env_; // ODBC environment handle
    SQLHDBC connection_; // ODBC connection handle
    std::unordered_map<std::string, SQLHSTMT> statements_; // ODBC statement handles for cursors
    std::unordered_map<std::string, std::string> cursorSql_; // Store original cursor SQL
    std::unordered_map<std::string, bool> executed_; // Track if cursor has been executed
    
    // Store parameter values to ensure lifetime during SQLExecute
    struct ParamStorage {
        std::vector<double> doubles;
        std::vector<std::string> strings;
        std::vector<long> bools;  // Store as 1/0 for PostgreSQL
        std::vector<SQLLEN> indicators;
    };
    std::unordered_map<std::string, ParamStorage> paramStorage_;
};

} // namespace trx::runtime