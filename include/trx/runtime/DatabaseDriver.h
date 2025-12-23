#pragma once

#include "trx/runtime/JsonValue.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace trx::runtime {

// Type aliases for SQL operations
using SqlValue = JsonValue;
struct SqlParameter {
    std::string name;
    SqlValue value;
};

struct TableColumn {
    std::string name;
    std::string typeName;
    bool isPrimaryKey{false};
    bool isNullable{true};
    std::optional<long> length{};
    std::optional<short> scale{};
    std::optional<std::string> defaultValue{};
};

/**
 * Abstract interface for database drivers.
 * This allows the TRX interpreter to work with different database backends.
 */
class DatabaseDriver {
public:
    virtual ~DatabaseDriver() = default;

    /**
     * Initialize the database connection and create necessary tables.
     */
    virtual void initialize() = 0;

    /**
     * Execute a SQL statement that doesn't return results (INSERT, UPDATE, DELETE).
     * @param sql The SQL statement to execute
     * @param params Parameters to bind to the statement
     */
    virtual void executeSql(const std::string& sql, const std::vector<SqlParameter>& params = {}) = 0;

    /**
     * Execute a SELECT statement and return results.
     * @param sql The SELECT SQL statement
     * @param params Parameters to bind
     * @return Vector of result rows, each row is a vector of SqlValue
     */
    virtual std::vector<std::vector<SqlValue>> querySql(const std::string& sql, const std::vector<SqlParameter>& params = {}) = 0;

    /**
     * Prepare a cursor for iterative access.
     * @param name Cursor name
     * @param sql The SELECT SQL statement
     * @param params Parameters to bind
     */
    virtual void openCursor(const std::string& name, const std::string& sql, const std::vector<SqlParameter>& params = {}) = 0;

    /**
     * Advance cursor to next row.
     * @param name Cursor name
     * @return true if there is a next row, false if no more rows
     */
    virtual bool cursorNext(const std::string& name) = 0;

    /**
     * Get the current row from a cursor.
     * @param name Cursor name
     * @return Current row as vector of SqlValue
     */
    virtual std::vector<SqlValue> cursorGetRow(const std::string& name) = 0;

    /**
     * Close a cursor.
     * @param name Cursor name
     */
    virtual void closeCursor(const std::string& name) = 0;

    /**
     * Create or migrate a table to match the given schema.
     * @param tableName Name of the table
     * @param columns Column definitions
     */
    virtual void createOrMigrateTable(const std::string& tableName, const std::vector<TableColumn>& columns) = 0;

    /**
     * Get the schema of an existing table.
     * @param tableName Name of the table
     * @return Vector of column definitions, or empty if table doesn't exist
     */
    virtual std::vector<TableColumn> getTableSchema(const std::string& tableName) = 0;

    /**
     * Check if currently in a transaction.
     * @return true if in a transaction, false otherwise
     */
    virtual bool isInTransaction() = 0;

    /**
     * Begin a transaction.
     */
    virtual void beginTransaction() = 0;

    /**
     * Commit a transaction.
     */
    virtual void commitTransaction() = 0;

    /**
     * Rollback a transaction.
     */
    virtual void rollbackTransaction() = 0;
};

/**
 * Factory function to create database drivers.
 */
enum class DatabaseType {
    SQLITE,
    POSTGRESQL,
    DB2,
    ODBC
};

/**
 * Configuration for database connections.
 */
struct DatabaseConfig {
    DatabaseType type;
    std::string connectionString;  // For ODBC, PostgreSQL, etc.
    std::string databasePath;      // For SQLite file path
    std::string host;
    std::string port;
    std::string username;
    std::string password;
    std::string databaseName;
};

/**
 * Create a database driver instance.
 */
std::unique_ptr<DatabaseDriver> createDatabaseDriver(const DatabaseConfig& config);

} // namespace trx::runtime