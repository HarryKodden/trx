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
    // Clean up cursors
    for (auto& [name, res] : cursors_) {
        if (res) {
            PQclear(res);
        }
    }
    cursors_.clear();

    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

void PostgreSQLDriver::initialize() {
    std::string conninfo = "host=" + config_.host + " port=" + config_.port +
                           " dbname=" + config_.databaseName + " user=" + config_.username +
                           " password=" + config_.password;
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
    std::vector<const char*> paramValues;
    std::vector<int> paramLengths;
    std::vector<int> paramFormats;
    for (const auto& param : params) {
        if (std::holds_alternative<std::string>(param.value.data)) {
            paramValues.push_back(std::get<std::string>(param.value.data).c_str());
            paramLengths.push_back(std::get<std::string>(param.value.data).size());
            paramFormats.push_back(0); // text
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
    // For simplicity, execute the query and store the result
    auto results = querySql(sql, params);
    // Store as a fake cursor, but since we have all results, simulate
    // In real implementation, use DECLARE CURSOR
    // For now, store the results in a way
    // But since cursors_ is PGresult*, and we have vector, perhaps store the vector somehow
    // For simplicity, since the interface expects PGresult*, but we can't store vector in PGresult*
    // Perhaps change the interface, but for now, execute and store empty PGresult or something
    // Actually, for cursors, PostgreSQL uses DECLARE name CURSOR FOR sql; then FETCH
    std::string declareSql = "DECLARE " + name + " CURSOR FOR " + sql;
    executeSql(declareSql, params);
    // Then fetch all at once for simplicity
    std::string fetchSql = "FETCH ALL FROM " + name;
    PGresult* res = PQexec(conn_, fetchSql.c_str());
    checkPGresult(res, conn_, "openCursor");
    cursors_[name] = res;
}

bool PostgreSQLDriver::cursorNext(const std::string& name) {
    auto it = cursors_.find(name);
    if (it == cursors_.end()) {
        throw std::runtime_error("Cursor not found: " + name);
    }
    PGresult* res = it->second;
    static int currentRow = 0; // Hack, should be per cursor
    int nrows = PQntuples(res);
    if (currentRow < nrows) {
        ++currentRow;
        return true;
    }
    return false;
}

std::vector<SqlValue> PostgreSQLDriver::cursorGetRow(const std::string& name) {
    auto it = cursors_.find(name);
    if (it == cursors_.end()) {
        throw std::runtime_error("Cursor not found: " + name);
    }
    PGresult* res = it->second;
    static int currentRow = 0; // Hack
    int ncols = PQnfields(res);
    std::vector<SqlValue> row;
    for (int j = 0; j < ncols; ++j) {
        if (PQgetisnull(res, currentRow - 1, j)) {
            row.emplace_back(SqlValue(nullptr));
        } else {
            std::string val = PQgetvalue(res, currentRow - 1, j);
            // Parse as before
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
    return row;
}

void PostgreSQLDriver::closeCursor(const std::string& name) {
    auto it = cursors_.find(name);
    if (it != cursors_.end()) {
        std::string closeSql = "CLOSE " + name;
        executeSql(closeSql, {});
        PQclear(it->second);
        cursors_.erase(it);
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

} // namespace trx::runtime