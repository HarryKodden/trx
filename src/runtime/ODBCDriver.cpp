#include "trx/runtime/ODBCDriver.h"

#include <sql.h>
#include <sqlext.h>
#include <iostream>
#include <sstream>
#include <cstring>

namespace trx::runtime {

namespace {

// Helper function to check ODBC return codes
void checkODBC(SQLRETURN ret, [[maybe_unused]] SQLHANDLE handle, [[maybe_unused]] SQLSMALLINT type, const std::string& operation) {

    std::cout << "ODBC: " << ret << std::endl;
    
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        std::stringstream ss;
        ss << "ODBC " << operation << " failed: SQL code " << ret;
        throw std::runtime_error(ss.str());
    }
}

} // namespace

ODBCDriver::ODBCDriver(const DatabaseConfig& config)
    : config_(config), env_(nullptr), connection_(nullptr) {}

ODBCDriver::~ODBCDriver() {
    // Clean up cursors
    for (auto& [name, stmt] : statements_) {
        if (stmt) {
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        }
    }
    statements_.clear();

    if (connection_) {
        SQLDisconnect(connection_);
        SQLFreeHandle(SQL_HANDLE_DBC, connection_);
        connection_ = nullptr;
    }

    if (env_) {
        SQLFreeHandle(SQL_HANDLE_ENV, env_);
        env_ = nullptr;
    }
}

void ODBCDriver::initialize() {
    // Allocate environment handle
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env_);
    checkODBC(ret, env_, SQL_HANDLE_ENV, "SQLAllocHandle(ENV)");

    // Set ODBC version
    ret = SQLSetEnvAttr(env_, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);
    checkODBC(ret, env_, SQL_HANDLE_ENV, "SQLSetEnvAttr");

    // Allocate connection handle
    ret = SQLAllocHandle(SQL_HANDLE_DBC, env_, &connection_);
    checkODBC(ret, env_, SQL_HANDLE_DBC, "SQLAllocHandle(DBC)");

    // Connect using connection string
    SQLCHAR outConnStr[1024];
    SQLSMALLINT outConnStrLen;
    ret = SQLDriverConnect(connection_, nullptr,
                          reinterpret_cast<SQLCHAR*>(const_cast<char*>(config_.connectionString.c_str())),
                          SQL_NTS, outConnStr, sizeof(outConnStr), &outConnStrLen, SQL_DRIVER_COMPLETE);
    checkODBC(ret, connection_, SQL_HANDLE_DBC, "SQLDriverConnect");

    std::cout << "ODBCDriver: Connected to database" << std::endl;
}

void ODBCDriver::executeSql(const std::string& sql, const std::vector<SqlParameter>& params) {
    SQLHSTMT stmt;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, connection_, &stmt);
    checkODBC(ret, connection_, SQL_HANDLE_DBC, "SQLAllocHandle(STMT)");

    try {
        // Prepare statement
        ret = SQLPrepare(stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.c_str())), SQL_NTS);
        checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLPrepare");

        // Bind parameters
        for (size_t i = 0; i < params.size(); ++i) {
            const auto& param = params[i];
            SQLLEN indicator = SQL_NTS;

            if (std::holds_alternative<double>(param.value.data)) {
                double val = std::get<double>(param.value.data);
                ret = SQLBindParameter(stmt, i + 1, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_DOUBLE, 0, 0, &val, 0, &indicator);
            } else if (std::holds_alternative<std::string>(param.value.data)) {
                const std::string& val = std::get<std::string>(param.value.data);
                ret = SQLBindParameter(stmt, i + 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, val.size(), 0,
                                     const_cast<char*>(val.c_str()), val.size(), &indicator);
            } else if (std::holds_alternative<bool>(param.value.data)) {
                bool val = std::get<bool>(param.value.data);
                ret = SQLBindParameter(stmt, i + 1, SQL_PARAM_INPUT, SQL_C_BIT, SQL_BIT, 0, 0, &val, 0, &indicator);
            } else {
                indicator = SQL_NULL_DATA;
                ret = SQLBindParameter(stmt, i + 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 0, 0, nullptr, 0, &indicator);
            }
            checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLBindParameter");
        }

        // Execute
        ret = SQLExecute(stmt);
        checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLExecute");

    } catch (...) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        throw;
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
}

std::vector<std::vector<SqlValue>> ODBCDriver::querySql(const std::string& sql, const std::vector<SqlParameter>& params) {
    SQLHSTMT stmt;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, connection_, &stmt);
    checkODBC(ret, connection_, SQL_HANDLE_DBC, "SQLAllocHandle(STMT)");

    std::vector<std::vector<SqlValue>> results;

    try {
        // Prepare statement
        ret = SQLPrepare(stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.c_str())), SQL_NTS);
        checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLPrepare");

        // Bind parameters
        for (size_t i = 0; i < params.size(); ++i) {
            const auto& param = params[i];
            SQLLEN indicator = SQL_NTS;

            if (std::holds_alternative<double>(param.value.data)) {
                double val = std::get<double>(param.value.data);
                ret = SQLBindParameter(stmt, i + 1, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_DOUBLE, 0, 0, &val, 0, &indicator);
            } else if (std::holds_alternative<std::string>(param.value.data)) {
                const std::string& val = std::get<std::string>(param.value.data);
                ret = SQLBindParameter(stmt, i + 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, val.size(), 0, 
                                     const_cast<char*>(val.c_str()), val.size(), &indicator);
            } else if (std::holds_alternative<bool>(param.value.data)) {
                bool val = std::get<bool>(param.value.data);
                ret = SQLBindParameter(stmt, i + 1, SQL_PARAM_INPUT, SQL_C_BIT, SQL_BIT, 0, 0, &val, 0, &indicator);
            } else {
                indicator = SQL_NULL_DATA;
                ret = SQLBindParameter(stmt, i + 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 0, 0, nullptr, 0, &indicator);
            }
            checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLBindParameter");
        }

        // Execute
        ret = SQLExecute(stmt);
        checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLExecute");

        // Get column count
        SQLSMALLINT numCols;
        ret = SQLNumResultCols(stmt, &numCols);
        checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLNumResultCols");

        // Fetch rows
        while ((ret = SQLFetch(stmt)) == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
            std::vector<SqlValue> row;
            for (SQLSMALLINT i = 1; i <= numCols; ++i) {
                SQLLEN indicator;
                char buffer[1024];
                ret = SQLGetData(stmt, i, SQL_C_CHAR, buffer, sizeof(buffer), &indicator);
                if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
                    if (indicator == SQL_NULL_DATA) {
                        row.emplace_back(nullptr);
                    } else {
                        row.emplace_back(std::string(buffer));
                    }
                } else {
                    checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLGetData");
                }
            }
            results.push_back(std::move(row));
        }

        if (ret != SQL_NO_DATA) {
            checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLFetch");
        }

    } catch (...) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        throw;
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return results;
}

void ODBCDriver::openCursor(const std::string& name, const std::string& sql, const std::vector<SqlParameter>& params) {
    if (statements_.count(name)) {
        closeCursor(name);
    }

    SQLHSTMT stmt;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, connection_, &stmt);
    checkODBC(ret, connection_, SQL_HANDLE_DBC, "SQLAllocHandle(STMT)");

    try {
        // Prepare statement
        ret = SQLPrepare(stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.c_str())), SQL_NTS);
        checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLPrepare");

        // Bind parameters
        for (size_t i = 0; i < params.size(); ++i) {
            const auto& param = params[i];
            SQLLEN indicator = SQL_NTS;

            if (std::holds_alternative<double>(param.value.data)) {
                double val = std::get<double>(param.value.data);
                ret = SQLBindParameter(stmt, i + 1, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_DOUBLE, 0, 0, &val, 0, &indicator);
            } else if (std::holds_alternative<std::string>(param.value.data)) {
                const std::string& val = std::get<std::string>(param.value.data);
                ret = SQLBindParameter(stmt, i + 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, val.size(), 0, 
                                     const_cast<char*>(val.c_str()), val.size(), &indicator);
            } else if (std::holds_alternative<bool>(param.value.data)) {
                bool val = std::get<bool>(param.value.data);
                ret = SQLBindParameter(stmt, i + 1, SQL_PARAM_INPUT, SQL_C_BIT, SQL_BIT, 0, 0, &val, 0, &indicator);
            } else {
                indicator = SQL_NULL_DATA;
                ret = SQLBindParameter(stmt, i + 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 0, 0, nullptr, 0, &indicator);
            }
            checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLBindParameter");
        }

        // Execute
        ret = SQLExecute(stmt);
        checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLExecute");

        statements_[name] = stmt;

    } catch (...) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        throw;
    }
}

bool ODBCDriver::cursorNext(const std::string& name) {
    auto it = statements_.find(name);
    if (it == statements_.end()) {
        throw std::runtime_error("Cursor not found: " + name);
    }

    SQLRETURN ret = SQLFetch(it->second);
    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
        return true;
    } else if (ret == SQL_NO_DATA) {
        return false;
    } else {
        checkODBC(ret, it->second, SQL_HANDLE_STMT, "SQLFetch");
        return false;
    }
}

std::vector<SqlValue> ODBCDriver::cursorGetRow(const std::string& name) {
    auto it = statements_.find(name);
    if (it == statements_.end()) {
        throw std::runtime_error("Cursor not found: " + name);
    }

    SQLHSTMT stmt = it->second;

    // Get column count
    SQLSMALLINT numCols;
    SQLRETURN ret = SQLNumResultCols(stmt, &numCols);
    checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLNumResultCols");

    std::vector<SqlValue> row;
    for (SQLSMALLINT i = 1; i <= numCols; ++i) {
        SQLLEN indicator;
        char buffer[1024];
        ret = SQLGetData(stmt, i, SQL_C_CHAR, buffer, sizeof(buffer), &indicator);
        if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
            if (indicator == SQL_NULL_DATA) {
                row.emplace_back(nullptr);
            } else {
                row.emplace_back(std::string(buffer));
            }
        } else {
            checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLGetData");
        }
    }

    return row;
}

void ODBCDriver::closeCursor(const std::string& name) {
    auto it = statements_.find(name);
    if (it != statements_.end()) {
        SQLFreeHandle(SQL_HANDLE_STMT, it->second);
        statements_.erase(it);
    }
}

void ODBCDriver::createOrMigrateTable(const std::string& tableName, const std::vector<TableColumn>& columns) {
    // Check if table exists
    std::string checkSql = "SELECT 1 FROM " + tableName + " LIMIT 1";
    try {
        querySql(checkSql);
        // Table exists, skip creation for now
        return;
    } catch (...) {
        // Table doesn't exist, create it
    }

    std::stringstream sql;
    sql << "CREATE TABLE " << tableName << " (";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) sql << ", ";
        sql << columns[i].name << " ";

        if (columns[i].typeName == "INTEGER") {
            sql << "INTEGER";
        } else if (columns[i].typeName == "VARCHAR" || columns[i].typeName == "CHAR") {
            sql << "VARCHAR(" << (columns[i].length ? *columns[i].length : 255) << ")";
        } else if (columns[i].typeName == "DECIMAL") {
            sql << "DECIMAL(" << (columns[i].length ? *columns[i].length : 10) << ", " << (columns[i].scale ? *columns[i].scale : 2) << ")";
        } else if (columns[i].typeName == "BOOLEAN") {
            sql << "BOOLEAN";
        } else {
            sql << "VARCHAR(255)"; // Default
        }

        if (!columns[i].isNullable) {
            sql << " NOT NULL";
        }

        if (columns[i].isPrimaryKey) {
            sql << " PRIMARY KEY";
        }
    }
    sql << ")";

    executeSql(sql.str());
}

void ODBCDriver::beginTransaction() {
    SQLRETURN ret = SQLSetConnectAttr(connection_, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_OFF), 0);
    checkODBC(ret, connection_, SQL_HANDLE_DBC, "SQLSetConnectAttr");
}

bool ODBCDriver::isInTransaction() {
    SQLUINTEGER autoCommit;
    SQLRETURN ret = SQLGetConnectAttr(connection_, SQL_ATTR_AUTOCOMMIT, &autoCommit, 0, nullptr);
    checkODBC(ret, connection_, SQL_HANDLE_DBC, "SQLGetConnectAttr");
    return autoCommit == SQL_AUTOCOMMIT_OFF;
}

void ODBCDriver::commitTransaction() {
    SQLRETURN ret = SQLEndTran(SQL_HANDLE_DBC, connection_, SQL_COMMIT);
    checkODBC(ret, connection_, SQL_HANDLE_DBC, "SQLEndTran");
    ret = SQLSetConnectAttr(connection_, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), 0);
    checkODBC(ret, connection_, SQL_HANDLE_DBC, "SQLSetConnectAttr");
}

void ODBCDriver::rollbackTransaction() {
    SQLRETURN ret = SQLEndTran(SQL_HANDLE_DBC, connection_, SQL_ROLLBACK);
    checkODBC(ret, connection_, SQL_HANDLE_DBC, "SQLEndTran");
    ret = SQLSetConnectAttr(connection_, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), 0);
    checkODBC(ret, connection_, SQL_HANDLE_DBC, "SQLSetConnectAttr");
}

std::vector<TableColumn> ODBCDriver::getTableSchema(const std::string& tableName) {
    SQLHSTMT stmt;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, connection_, &stmt);
    checkODBC(ret, connection_, SQL_HANDLE_DBC, "SQLAllocHandle(STMT)");

    std::vector<TableColumn> columns;

    try {
        // Call SQLColumns to get column information
        // SQLColumns(hstmt, CatalogName, NameLength1, SchemaName, NameLength2, TableName, NameLength3, ColumnName, NameLength4)
        ret = SQLColumns(stmt,
                        nullptr, 0,  // Catalog name (nullptr for all catalogs)
                        nullptr, 0,  // Schema name (nullptr for all schemas)
                        reinterpret_cast<SQLCHAR*>(const_cast<char*>(tableName.c_str())), SQL_NTS,  // Table name
                        nullptr, 0); // Column name (nullptr for all columns)
        checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLColumns");

        // Bind result columns
        SQLCHAR columnName[256];
        SQLCHAR dataTypeStr[256];
        SQLLEN dataType;
        SQLLEN columnSize;
        SQLLEN decimalDigits;
        SQLLEN nullable;

        ret = SQLBindCol(stmt, 4, SQL_C_CHAR, columnName, sizeof(columnName), nullptr); // COLUMN_NAME
        checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLBindCol(COLUMN_NAME)");

        ret = SQLBindCol(stmt, 5, SQL_C_SLONG, &dataType, 0, nullptr); // DATA_TYPE
        checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLBindCol(DATA_TYPE)");

        ret = SQLBindCol(stmt, 7, SQL_C_CHAR, dataTypeStr, sizeof(dataTypeStr), nullptr); // TYPE_NAME
        checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLBindCol(TYPE_NAME)");

        ret = SQLBindCol(stmt, 8, SQL_C_SLONG, &columnSize, 0, nullptr); // COLUMN_SIZE
        checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLBindCol(COLUMN_SIZE)");

        ret = SQLBindCol(stmt, 9, SQL_C_SLONG, &decimalDigits, 0, nullptr); // DECIMAL_DIGITS
        checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLBindCol(DECIMAL_DIGITS)");

        ret = SQLBindCol(stmt, 11, SQL_C_SLONG, &nullable, 0, nullptr); // NULLABLE
        checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLBindCol(NULLABLE)");

        // Fetch rows
        while ((ret = SQLFetch(stmt)) == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
            TableColumn col;
            col.name = reinterpret_cast<char*>(columnName);

            // Map ODBC SQL data types to TRX types
            switch (dataType) {
                case SQL_INTEGER:
                case SQL_SMALLINT:
                case SQL_TINYINT:
                case SQL_BIGINT:
                    col.typeName = "INTEGER";
                    break;
                case SQL_CHAR:
                case SQL_VARCHAR:
                case SQL_LONGVARCHAR:
                case SQL_WCHAR:
                case SQL_WVARCHAR:
                case SQL_WLONGVARCHAR:
                    col.typeName = "CHAR";
                    if (columnSize != SQL_NO_TOTAL && columnSize > 0) {
                        col.length = static_cast<long>(columnSize);
                    }
                    break;
                case SQL_DECIMAL:
                case SQL_NUMERIC:
                case SQL_FLOAT:
                case SQL_REAL:
                case SQL_DOUBLE:
                    col.typeName = "DECIMAL";
                    if (columnSize != SQL_NO_TOTAL && columnSize > 0) {
                        col.length = static_cast<long>(columnSize);
                    }
                    if (decimalDigits != SQL_NO_TOTAL && decimalDigits >= 0) {
                        col.scale = static_cast<short>(decimalDigits);
                    }
                    break;
                case SQL_BIT:
                    col.typeName = "BOOLEAN";
                    break;
                default:
                    col.typeName = "CHAR"; // Default fallback
                    break;
            }

            col.isNullable = (nullable == SQL_NULLABLE);
            // Note: Primary key information would require a separate query using SQLPrimaryKeys
            // For now, we'll leave isPrimaryKey as false

            columns.push_back(col);
        }

        if (ret != SQL_NO_DATA) {
            checkODBC(ret, stmt, SQL_HANDLE_STMT, "SQLFetch");
        }

    } catch (...) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        throw;
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return columns;
}

} // namespace trx::runtime