#include "trx/runtime/PostgreSQLDriver.h"

#include <postgresql/libpq-fe.h>
#include <iostream>
#include <sstream>
#include <cstring>

namespace trx::runtime {

namespace {

// Helper function to check PostgreSQL result
void checkPGresult(PGresult* res, PGconn* conn, const std::string& operation) {
    if (!res || (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK)) {
        std::stringstream ss;
        ss << "PostgreSQL " << operation << " failed: " << PQerrorMessage(conn);
        PQclear(res);
        throw std::runtime_error(ss.str());
    }
}

} // namespace

PostgreSQLDriver::PostgreSQLDriver(const DatabaseConfig& config)
    : config_(config), conn_(nullptr) {}

PostgreSQLDriver::~PostgreSQLDriver() {
    // Clean up cursors - close any open cursors
    for (auto& [name, declared] : cursors_) {
        if (declared) {
            try {
                executeSql("CLOSE " + name);
            } catch (...) {
                // Ignore errors during cleanup
            }
        }
    }
    cursors_.clear();
    cursorSql_.clear();
    currentRows_.clear();

    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

void PostgreSQLDriver::initialize() {
    std::string conninfo;
    if (!config_.connectionString.empty()) {
        // Use connection string directly
        conninfo = config_.connectionString;
    } else {
        // Build from individual fields
        conninfo = "host=" + config_.host + " port=" + config_.port +
                   " dbname=" + config_.databaseName + " user=" + config_.username +
                   " password=" + config_.password;
    }
    conn_ = PQconnectdb(conninfo.c_str());
    if (PQstatus(conn_) != CONNECTION_OK) {
        std::stringstream ss;
        ss << "PostgreSQL connection failed: " << PQerrorMessage(conn_);
        PQfinish(conn_);
        conn_ = nullptr;
        throw std::runtime_error(ss.str());
    }
    std::cout << "PostgreSQLDriver: Connected to database" << std::endl;
}

void PostgreSQLDriver::executeSql(const std::string& sql, const std::vector<SqlParameter>& params) {
    // Convert ? placeholders to $1, $2, etc. for PostgreSQL
    std::string convertedSql = sql;
    size_t pos = 0;
    int paramIndex = 1;
    while ((pos = convertedSql.find('?', pos)) != std::string::npos) {
        std::string replacement = "$" + std::to_string(paramIndex++);
        convertedSql.replace(pos, 1, replacement);
        pos += replacement.length();
    }

    std::cout << "SQL EXEC: " << convertedSql << std::endl;

    std::vector<const char*> paramValues;
    std::vector<int> paramLengths;
    std::vector<int> paramFormats;
    for (const auto& param : params) {
        if (std::holds_alternative<std::string>(param.value.data)) {
            paramValues.push_back(std::get<std::string>(param.value.data).c_str());
            paramLengths.push_back(std::get<std::string>(param.value.data).size());
            paramFormats.push_back(0); // text
        } else if (std::holds_alternative<double>(param.value.data)) {
            double num = std::get<double>(param.value.data);
            // Check if it's a whole number (integer)
            if (num == static_cast<long long>(num)) {
                // Format as integer
                std::string val = std::to_string(static_cast<long long>(num));
                paramValues.push_back(val.c_str());
                paramLengths.push_back(val.size());
            } else {
                // Format as decimal
                std::string val = std::to_string(num);
                paramValues.push_back(val.c_str());
                paramLengths.push_back(val.size());
            }
            paramFormats.push_back(0);
        } else if (std::holds_alternative<bool>(param.value.data)) {
            paramValues.push_back(std::get<bool>(param.value.data) ? "true" : "false");
            paramLengths.push_back(0);
            paramFormats.push_back(0);
        } else {
            paramValues.push_back(nullptr);
            paramLengths.push_back(0);
            paramFormats.push_back(0);
        }
    }

    PGresult* res = PQexecParams(conn_, convertedSql.c_str(), params.size(),
                                 nullptr, paramValues.data(), paramLengths.data(),
                                 paramFormats.data(), 0);
    checkPGresult(res, conn_, "executeSql");
    PQclear(res);
}

std::vector<std::vector<SqlValue>> PostgreSQLDriver::querySql(const std::string& sql, const std::vector<SqlParameter>& params) {
    std::vector<const char*> paramValues;
    std::vector<int> paramLengths;
    std::vector<int> paramFormats;
    for (const auto& param : params) {
        if (std::holds_alternative<std::string>(param.value.data)) {
            paramValues.push_back(std::get<std::string>(param.value.data).c_str());
            paramLengths.push_back(std::get<std::string>(param.value.data).size());
            paramFormats.push_back(0);
        } else if (std::holds_alternative<double>(param.value.data)) {
            std::string val = std::to_string(std::get<double>(param.value.data));
            paramValues.push_back(val.c_str());
            paramLengths.push_back(val.size());
            paramFormats.push_back(0);
        } else if (std::holds_alternative<bool>(param.value.data)) {
            paramValues.push_back(std::get<bool>(param.value.data) ? "true" : "false");
            paramLengths.push_back(0);
            paramFormats.push_back(0);
        } else {
            paramValues.push_back(nullptr);
            paramLengths.push_back(0);
            paramFormats.push_back(0);
        }
    }

    PGresult* res = PQexecParams(conn_, sql.c_str(), params.size(),
                                 nullptr, paramValues.data(), paramLengths.data(),
                                 paramFormats.data(), 0);
    checkPGresult(res, conn_, "querySql");

    std::vector<std::vector<SqlValue>> results;
    int nrows = PQntuples(res);
    int ncols = PQnfields(res);
    for (int i = 0; i < nrows; ++i) {
        std::vector<SqlValue> row;
        for (int j = 0; j < ncols; ++j) {
            if (PQgetisnull(res, i, j)) {
                row.emplace_back(SqlValue(nullptr));
            } else {
                std::string val = PQgetvalue(res, i, j);
                // Try to parse as number or bool
                if (val == "t" || val == "f") {
                    row.emplace_back(SqlValue(val == "t"));
                } else {
                    try {
                        double num = std::stod(val);
                        row.emplace_back(SqlValue(num));
                    } catch (...) {
                        row.emplace_back(SqlValue(val));
                    }
                }
            }
        }
        results.push_back(std::move(row));
    }
    PQclear(res);
    return results;
}

void PostgreSQLDriver::openCursor(const std::string& name, const std::string& sql, const std::vector<SqlParameter>& params) {
    closeCursor(name); // Close if already exists

    std::string declareSql = "DECLARE " + name + " CURSOR FOR " + sql;
    executeSql(declareSql, params);
    
    // Store the original SQL for potential reopening
    cursorSql_[name] = sql;
    
    // In PostgreSQL, DECLARE ... CURSOR FOR ... automatically opens the cursor
    // No separate OPEN statement needed
    cursors_[name] = true;
}

void PostgreSQLDriver::openDeclaredCursor(const std::string& name) {
    auto it = cursors_.find(name);
    if (it == cursors_.end()) {
        throw std::runtime_error("Cursor not declared: " + name);
    }
    
    // In PostgreSQL, DECLARE ... CURSOR FOR ... automatically opens the cursor
    // No separate OPEN statement needed
}

void PostgreSQLDriver::openDeclaredCursorWithParams(const std::string& name, const std::vector<SqlParameter>& params) {
    auto sqlIt = cursorSql_.find(name);
    if (sqlIt == cursorSql_.end()) {
        throw std::runtime_error("Cursor not declared with USING support: " + name);
    }
    
    // Close existing cursor if open
    closeCursor(name);
    
    // Re-declare the cursor with new parameters
    std::string declareSql = "DECLARE " + name + " CURSOR FOR " + sqlIt->second;
    executeSql(declareSql, params);
    
    cursors_[name] = true;
}

bool PostgreSQLDriver::cursorNext(const std::string& name) {
    auto it = cursors_.find(name);
    if (it == cursors_.end() || !it->second) {
        throw std::runtime_error("Cursor not found: " + name);
    }

    std::string fetchSql = "FETCH NEXT FROM " + name;
    PGresult* res = PQexec(conn_, fetchSql.c_str());
    checkPGresult(res, conn_, "cursorNext");

    int nrows = PQntuples(res);
    if (nrows > 0) {
        // Extract the row
        int ncols = PQnfields(res);
        std::vector<SqlValue> row;
        for (int j = 0; j < ncols; ++j) {
            if (PQgetisnull(res, 0, j)) {
                row.emplace_back(SqlValue(nullptr));
            } else {
                std::string val = PQgetvalue(res, 0, j);
                // Parse value
                if (val == "t" || val == "f") {
                    row.emplace_back(SqlValue(val == "t"));
                } else {
                    try {
                        double num = std::stod(val);
                        row.emplace_back(SqlValue(num));
                    } catch (...) {
                        row.emplace_back(SqlValue(val));
                    }
                }
            }
        }
        currentRows_[name] = std::move(row);
        PQclear(res);
        return true;
    } else {
        PQclear(res);
        return false;
    }
}

std::vector<SqlValue> PostgreSQLDriver::cursorGetRow(const std::string& name) {
    auto it = currentRows_.find(name);
    if (it == currentRows_.end()) {
        throw std::runtime_error("No current row for cursor: " + name);
    }
    return it->second;
}

void PostgreSQLDriver::closeCursor(const std::string& name) {
    auto it = cursors_.find(name);
    if (it != cursors_.end()) {
        std::string closeSql = "CLOSE " + name;
        try {
            executeSql(closeSql, {});
        } catch (const std::exception&) {
            // Ignore errors when closing cursors
        }
        cursors_.erase(it);
        currentRows_.erase(name);
        // Keep cursorSql_ for potential reopening
    }
}

void PostgreSQLDriver::createOrMigrateTable(const std::string& tableName, const std::vector<TableColumn>& columns) {
    // Check if table exists
    std::string checkSql = "SELECT 1 FROM information_schema.tables WHERE table_name = $1";
    auto results = querySql(checkSql, {SqlParameter{"", SqlValue(tableName)}});
    if (!results.empty()) {
        // Table exists, skip for now
        return;
    }

    std::stringstream ss;
    ss << "CREATE TABLE " << tableName << " (";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << columns[i].name << " ";
        // Map types
        if (columns[i].typeName == "INTEGER") ss << "INTEGER";
        else if (columns[i].typeName == "VARCHAR") {
            ss << "VARCHAR";
            if (columns[i].length.has_value()) {
                ss << "(" << columns[i].length.value() << ")";
            }
        }
        else if (columns[i].typeName == "DECIMAL") {
            ss << "DECIMAL";
            if (columns[i].length.has_value() && columns[i].scale.has_value()) {
                ss << "(" << columns[i].length.value() << "," << columns[i].scale.value() << ")";
            }
        }
        else if (columns[i].typeName == "BOOLEAN") ss << "BOOLEAN";
        else ss << "TEXT";
        if (columns[i].isPrimaryKey) ss << " PRIMARY KEY";
        if (!columns[i].isNullable) ss << " NOT NULL";
    }
    ss << ")";
    executeSql(ss.str(), {});
}

void PostgreSQLDriver::beginTransaction() {
    executeSql("BEGIN", {});
}

bool PostgreSQLDriver::isInTransaction() {
    return PQtransactionStatus(conn_) == PQTRANS_INTRANS || PQtransactionStatus(conn_) == PQTRANS_INERROR;
}

void PostgreSQLDriver::commitTransaction() {
    executeSql("COMMIT", {});
}

void PostgreSQLDriver::rollbackTransaction() {
    executeSql("ROLLBACK", {});
}

std::vector<TableColumn> PostgreSQLDriver::getTableSchema(const std::string& tableName) {
    // Query information_schema for column details
    std::string sql = R"(
        SELECT column_name, data_type, character_maximum_length, numeric_precision, numeric_scale, 
               is_nullable, column_default
        FROM information_schema.columns 
        WHERE table_name = $1 
        ORDER BY ordinal_position
    )";
    
    auto results = querySql(sql, {{"", tableName}});
    
    std::vector<TableColumn> columns;
    for (const auto& row : results) {
        if (row.size() >= 7) {
            TableColumn col;
            col.name = row[0].asString(); // column_name
            std::string dataType = row[1].asString(); // data_type
            
            // Map PostgreSQL types to TRX types
            if (dataType == "integer" || dataType == "bigint" || dataType == "smallint") {
                col.typeName = "INTEGER";
            } else if (dataType == "character varying" || dataType == "text" || dataType == "character") {
                col.typeName = "CHAR";
                if (!row[2].isNull()) {
                    col.length = static_cast<long>(row[2].asNumber()); // character_maximum_length
                }
            } else if (dataType == "numeric" || dataType == "decimal" || dataType == "real" || dataType == "double precision") {
                col.typeName = "DECIMAL";
                if (!row[3].isNull()) {
                    col.length = static_cast<long>(row[3].asNumber()); // numeric_precision
                }
                if (!row[4].isNull()) {
                    col.scale = static_cast<short>(row[4].asNumber()); // numeric_scale
                }
            } else if (dataType == "boolean") {
                col.typeName = "BOOLEAN";
            } else {
                col.typeName = "CHAR"; // Default
            }
            
            col.isNullable = row[5].asString() == "YES"; // is_nullable
            col.defaultValue = row[6].isNull() ? std::optional<std::string>{} : std::optional<std::string>{row[6].asString()}; // column_default
            
            columns.push_back(col);
        }
    }
    
    return columns;
}

} // namespace trx::runtime