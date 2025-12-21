#pragma once

#include "trx/runtime/DatabaseDriver.h"

#include <sqlite3.h>
#include <memory>
#include <string>
#include <unordered_map>

namespace trx::runtime {

/**
 * SQLite implementation of the DatabaseDriver interface.
 */
class SQLiteDriver : public DatabaseDriver {
public:
    explicit SQLiteDriver(const DatabaseConfig& config);
    ~SQLiteDriver() override;

    void initialize() override;
    void executeSql(const std::string& sql, const std::vector<SqlParameter>& params = {}) override;
    std::vector<std::vector<SqlValue>> querySql(const std::string& sql, const std::vector<SqlParameter>& params = {}) override;
    void openCursor(const std::string& name, const std::string& sql, const std::vector<SqlParameter>& params = {}) override;
    bool cursorNext(const std::string& name) override;
    std::vector<SqlValue> cursorGetRow(const std::string& name) override;
    void closeCursor(const std::string& name) override;
    void createOrMigrateTable(const std::string& tableName, const std::vector<TableColumn>& columns) override;
    void beginTransaction() override;
    void commitTransaction() override;
    void rollbackTransaction() override;

private:
    DatabaseConfig config_;
    sqlite3* db_;
    std::unordered_map<std::string, sqlite3_stmt*> cursors_;
    
    void bindParameters(sqlite3_stmt* stmt, const std::vector<SqlParameter>& params);
};

} // namespace trx::runtime