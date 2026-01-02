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
    // Check transaction state and handle error conditions
    PGTransactionStatusType txStatus = PQtransactionStatus(conn_);
    
    // Special handling for RELEASE SAVEPOINT when transaction is in error state
    // In this case, we should actually ROLLBACK TO SAVEPOINT instead
    bool isReleaseSavepoint = (sql.find("RELEASE SAVEPOINT") != std::string::npos);
    if (txStatus == PQTRANS_INERROR && isReleaseSavepoint) {
        // Convert RELEASE to ROLLBACK TO
        size_t pos = sql.find("RELEASE SAVEPOINT");
        std::string savepointName = sql.substr(pos + 17);  // "RELEASE SAVEPOINT" is 17 chars
        // Trim leading space
        size_t firstNonSpace = savepointName.find_first_not_of(" \t\n\r");
        if (firstNonSpace != std::string::npos) {
            savepointName = savepointName.substr(firstNonSpace);
        }
        std::string rollbackSql = "ROLLBACK TO SAVEPOINT " + savepointName;
        PGresult* res = PQexec(conn_, rollbackSql.c_str());
        if (!res || (PQresultStatus(res) != PGRES_COMMAND_OK)) {
            std::string error = PQerrorMessage(conn_);
            PQclear(res);
            throw std::runtime_error("PostgreSQL ROLLBACK TO SAVEPOINT failed: " + error);
        }
        PQclear(res);
        return;  // Done, don't execute the original RELEASE command
    }
    
    // Note: We do NOT automatically ROLLBACK on PQTRANS_INERROR here!
    // PostgreSQL transactions remain in error state until explicitly:
    // 1. ROLLBACK TO SAVEPOINT (if using savepoints)
    // 2. ROLLBACK (to end the transaction)
    // The Interpreter's exception handling will decide which is appropriate.
    
    // Convert ? placeholders to $1, $2, etc. for PostgreSQL
    std::string convertedSql = sql;
    size_t pos = 0;
    int paramIndex = 1;
    while ((pos = convertedSql.find('?', pos)) != std::string::npos) {
        std::string replacement = "$" + std::to_string(paramIndex++);
        convertedSql.replace(pos, 1, replacement);
        pos += replacement.length();
    }

    std::vector<std::string> paramStrings;
    std::vector<const char*> paramValues;
    std::vector<int> paramLengths;
    std::vector<int> paramFormats;
    paramStrings.reserve(params.size()); // prevent reallocation that invalidates c_str()
    paramValues.reserve(params.size());
    paramLengths.reserve(params.size());
    paramFormats.reserve(params.size());
    for (const auto& param : params) {
        if (std::holds_alternative<std::string>(param.value.data)) {
            std::string str = std::get<std::string>(param.value.data);
            paramStrings.push_back(str);
            paramValues.push_back(paramStrings.back().c_str());
            paramLengths.push_back(0); // 0 for text format (null-terminated)
            paramFormats.push_back(0); // text format
        } else if (std::holds_alternative<double>(param.value.data)) {
            double num = std::get<double>(param.value.data);
            // Check if it's a whole number (integer)
            if (num == static_cast<long long>(num)) {
                // Format as integer
                paramStrings.push_back(std::to_string(static_cast<long long>(num)));
            } else {
                // Format as decimal
                paramStrings.push_back(std::to_string(num));
            }
            paramValues.push_back(paramStrings.back().c_str());
            paramLengths.push_back(0); // 0 for text format (null-terminated)
            paramFormats.push_back(0);
        } else if (std::holds_alternative<bool>(param.value.data)) {
            paramStrings.push_back(std::get<bool>(param.value.data) ? "true" : "false");
            paramValues.push_back(paramStrings.back().c_str());
            paramLengths.push_back(0); // 0 for text format (null-terminated)
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
    // Convert ? placeholders to $1, $2, etc. for PostgreSQL
    std::string convertedSql = sql;
    size_t pos = 0;
    int paramIndex = 1;
    while ((pos = convertedSql.find('?', pos)) != std::string::npos) {
        std::string replacement = "$" + std::to_string(paramIndex++);
        convertedSql.replace(pos, 1, replacement);
        pos += replacement.length();
    }

    std::vector<std::string> paramStrings;
    std::vector<const char*> paramValues;
    std::vector<int> paramLengths;
    std::vector<int> paramFormats;
    paramStrings.reserve(params.size()); // prevent reallocation that invalidates c_str()
    paramValues.reserve(params.size());
    paramLengths.reserve(params.size());
    paramFormats.reserve(params.size());
    for (const auto& param : params) {
        if (std::holds_alternative<std::string>(param.value.data)) {
            paramStrings.push_back(std::get<std::string>(param.value.data));
            paramValues.push_back(paramStrings.back().c_str());
            paramLengths.push_back(0); // 0 for text format (null-terminated)
            paramFormats.push_back(0); // text
        } else if (std::holds_alternative<double>(param.value.data)) {
            double num = std::get<double>(param.value.data);
            // Check if it's a whole number (integer)
            if (num == static_cast<long long>(num)) {
                // Format as integer
                paramStrings.push_back(std::to_string(static_cast<long long>(num)));
            } else {
                // Format as decimal
                paramStrings.push_back(std::to_string(num));
            }
            paramValues.push_back(paramStrings.back().c_str());
            paramLengths.push_back(0); // 0 for text format (null-terminated)
            paramFormats.push_back(0);
        } else if (std::holds_alternative<bool>(param.value.data)) {
            paramStrings.push_back(std::get<bool>(param.value.data) ? "true" : "false");
            paramValues.push_back(paramStrings.back().c_str());
            paramLengths.push_back(0); // 0 for text format (null-terminated)
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

    // Check if SQL contains placeholders
    bool hasPlaceholders = (sql.find('?') != std::string::npos);
    
    // Store the original SQL for potential reopening with different parameters
    cursorSql_[name] = sql;
    
    // If there are placeholders but no parameters provided, don't execute yet
    // This handles the pattern: DECLARE cursor ... WHERE x = ?; OPEN cursor USING :param;
    if (hasPlaceholders && params.empty()) {
        return;  // Just store the SQL, don't execute DECLARE yet
    }
    
    // If we have parameters, substitute them into the query
    std::string query = sql;
    if (!params.empty()) {
        // Replace ? with actual values for DECLARE
        size_t paramIdx = 0;
        size_t pos = 0;
        while ((pos = query.find('?', pos)) != std::string::npos && paramIdx < params.size()) {
            const auto& param = params[paramIdx++];
            std::string value;
            if (std::holds_alternative<std::string>(param.value.data)) {
                // Quote string values and escape single quotes
                std::string str = std::get<std::string>(param.value.data);
                std::string escaped;
                for (char c : str) {
                    if (c == '\'') {
                        escaped += "''";  // Escape single quotes
                    } else {
                        escaped += c;
                    }
                }
                value = "'" + escaped + "'";
            } else if (std::holds_alternative<double>(param.value.data)) {
                double num = std::get<double>(param.value.data);
                if (num == static_cast<long long>(num)) {
                    value = std::to_string(static_cast<long long>(num));
                } else {
                    value = std::to_string(num);
                }
            } else if (std::holds_alternative<bool>(param.value.data)) {
                value = std::get<bool>(param.value.data) ? "TRUE" : "FALSE";
            } else {
                value = "NULL";
            }
            query.replace(pos, 1, value);
            pos += value.length();
        }
    }

    std::string declareSql = "DECLARE " + name + " CURSOR FOR " + query;
    
    // Execute DECLARE directly without parameter binding
    PGresult* res = PQexec(conn_, declareSql.c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::string error = PQerrorMessage(conn_);
        PQclear(res);
        throw std::runtime_error("PostgreSQL openCursor failed: " + error);
    }
    PQclear(res);
    
    // In PostgreSQL, DECLARE ... CURSOR FOR ... automatically opens the cursor
    // No separate OPEN statement needed
    cursors_[name] = true;
}

void PostgreSQLDriver::openDeclaredCursor(const std::string& name) {
    auto sqlIt = cursorSql_.find(name);
    if (sqlIt != cursorSql_.end()) {
        closeCursor(name);
        // Note: sqlIt->second may contain ? placeholders, but for openDeclaredCursor
        // without params, we should have already substituted them or it's an error
        std::string declareSql = "DECLARE " + name + " CURSOR FOR " + sqlIt->second;
        
        // Execute DECLARE directly without parameter binding
        PGresult* res = PQexec(conn_, declareSql.c_str());
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::string error = PQerrorMessage(conn_);
            PQclear(res);
            throw std::runtime_error("PostgreSQL openDeclaredCursor failed: " + error);
        }
        PQclear(res);
        
        // In PostgreSQL, DECLARE automatically opens the cursor
        cursors_[name] = true;
    } else {
        throw std::runtime_error("Cursor not declared: " + name);
    }
}

void PostgreSQLDriver::openDeclaredCursorWithParams(const std::string& name, const std::vector<SqlParameter>& params) {
    auto sqlIt = cursorSql_.find(name);
    if (sqlIt == cursorSql_.end()) {
        throw std::runtime_error("Cursor not declared with USING support: " + name);
    }
    
    // Close existing cursor if open
    closeCursor(name);
    
    std::string query = sqlIt->second;
    // Replace ? with actual values
    size_t paramIdx = 0;
    size_t pos = 0;
    while ((pos = query.find('?', pos)) != std::string::npos && paramIdx < params.size()) {
        const auto& param = params[paramIdx++];
        std::string value;
        if (std::holds_alternative<std::string>(param.value.data)) {
            // Quote string values and escape single quotes
            std::string str = std::get<std::string>(param.value.data);
            std::string escaped;
            for (char c : str) {
                if (c == '\'') {
                    escaped += "''";  // Escape single quotes
                } else {
                    escaped += c;
                }
            }
            value = "'" + escaped + "'";
        } else if (std::holds_alternative<double>(param.value.data)) {
            double num = std::get<double>(param.value.data);
            if (num == static_cast<long long>(num)) {
                value = std::to_string(static_cast<long long>(num));
            } else {
                value = std::to_string(num);
            }
        } else if (std::holds_alternative<bool>(param.value.data)) {
            value = std::get<bool>(param.value.data) ? "TRUE" : "FALSE";
        } else {
            value = "NULL";
        }
        query.replace(pos, 1, value);
        pos += value.length();
    }
    
    // Re-declare the cursor with new parameters
    std::string declareSql = "DECLARE " + name + " CURSOR FOR " + query;
    
    // Execute DECLARE directly without parameter binding
    PGresult* res = PQexec(conn_, declareSql.c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::string error = PQerrorMessage(conn_);
        PQclear(res);
        throw std::runtime_error("PostgreSQL openDeclaredCursorWithParams failed: " + error);
    }
    PQclear(res);
    
    // In PostgreSQL, DECLARE automatically opens the cursor
    
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