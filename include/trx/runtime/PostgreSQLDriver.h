#pragma once

#include "trx/runtime/DatabaseDriver.h"

#include <postgresql/libpq-fe.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace trx::runtime {

/**
 * PostgreSQL implementation of the DatabaseDriver interface.
 */
class PostgreSQLDriver : public DatabaseDriver {
public:
    explicit PostgreSQLDriver(const DatabaseConfig& config);
    ~PostgreSQLDriver() override;

    void initialize() override;
    void executeSql(const std::string& sql, const std::vector<SqlParameter>& params = {}) override;
    std::vector<std::vector<SqlValue>> querySql(const std::string& sql, const std::vector<SqlParameter>& params = {}) override;
    void openCursor(const std::string& name, const std::string& sql, const std::vector<SqlParameter>& params = {}) override;
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
    PGconn* conn_;
    std::unordered_map<std::string, bool> cursors_; // Just track if cursor is declared
    std::unordered_map<std::string, std::vector<SqlValue>> currentRows_;
};

} // namespace trx::runtime