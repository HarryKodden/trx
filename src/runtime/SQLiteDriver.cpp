#include "trx/runtime/SQLiteDriver.h"

#include <sqlite3.h>
#include <iostream>
#include <sstream>
#include <cmath>

namespace trx::runtime {

SQLiteDriver::SQLiteDriver(const DatabaseConfig& config)
    : config_(config), db_(nullptr) {}

SQLiteDriver::~SQLiteDriver() {
    // Clean up cursors
    for (auto& [name, stmt] : cursors_) {
        if (stmt) {
            sqlite3_finalize(stmt);
        }
    }
    cursors_.clear();

    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void SQLiteDriver::initialize() {
        // Open database
        std::string dbPath = config_.databasePath.empty() ? ":memory:" : config_.databasePath;
        if (sqlite3_open(dbPath.c_str(), &db_) != SQLITE_OK) {
            throw std::runtime_error("Failed to open SQLite database: " + std::string(sqlite3_errmsg(db_)));
        }

        // Note: Table creation is now handled by the Interpreter when processing TableDecl declarations
        // This allows for explicit schema management in TRX code rather than hardcoded SQL
    }

void SQLiteDriver::executeSql(const std::string& sql, const std::vector<SqlParameter>& params) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error("Failed to prepare SQL: " + std::string(sqlite3_errmsg(db_)));
        }

        bindParameters(stmt, params);

        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            std::string error = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            throw std::runtime_error("Failed to execute SQL: " + error);
        }

        sqlite3_finalize(stmt);
    }

std::vector<std::vector<SqlValue>> SQLiteDriver::querySql(const std::string& sql, const std::vector<SqlParameter>& params) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error("Failed to prepare SQL: " + std::string(sqlite3_errmsg(db_)));
        }

        bindParameters(stmt, params);

        std::vector<std::vector<SqlValue>> results;
        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            std::vector<SqlValue> row;
            int columnCount = sqlite3_column_count(stmt);
            for (int i = 0; i < columnCount; ++i) {
                SqlValue value;

                switch (sqlite3_column_type(stmt, i)) {
                    case SQLITE_INTEGER:
                        value = SqlValue(static_cast<double>(sqlite3_column_int64(stmt, i)));
                        break;
                    case SQLITE_FLOAT:
                        value = SqlValue(sqlite3_column_double(stmt, i));
                        break;
                    case SQLITE_TEXT:
                        value = SqlValue(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, i))));
                        break;
                    case SQLITE_NULL:
                        value = SqlValue(nullptr);
                        break;
                    default:
                        value = SqlValue(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, i))));
                        break;
                }

                row.push_back(value);
            }
            results.push_back(row);
        }

        if (rc != SQLITE_DONE) {
            std::string error = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            throw std::runtime_error("Failed to execute query: " + error);
        }

        sqlite3_finalize(stmt);
        return results;
    }

void SQLiteDriver::openCursor(const std::string& name, const std::string& sql, const std::vector<SqlParameter>& params) {
        closeCursor(name); // Close if already exists

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error("Failed to prepare cursor SQL: " + std::string(sqlite3_errmsg(db_)));
        }

        bindParameters(stmt, params);
        cursors_[name] = stmt;
    }

bool SQLiteDriver::cursorNext(const std::string& name) {
        auto it = cursors_.find(name);
        if (it == cursors_.end()) {
            throw std::runtime_error("Cursor not found: " + name);
        }

        int rc = sqlite3_step(it->second);
        if (rc == SQLITE_ROW) {
            return true;
        } else if (rc == SQLITE_DONE) {
            return false;
        } else {
            throw std::runtime_error("Failed to step cursor: " + std::string(sqlite3_errmsg(db_)));
        }
    }

std::vector<SqlValue> SQLiteDriver::cursorGetRow(const std::string& name) {
        auto it = cursors_.find(name);
        if (it == cursors_.end()) {
            throw std::runtime_error("Cursor not found: " + name);
        }

        sqlite3_stmt* stmt = it->second;
        int columnCount = sqlite3_column_count(stmt);
        std::vector<SqlValue> row;

        for (int i = 0; i < columnCount; ++i) {
            SqlValue value;

            switch (sqlite3_column_type(stmt, i)) {
                case SQLITE_INTEGER:
                    value = SqlValue(static_cast<double>(sqlite3_column_int64(stmt, i)));
                    break;
                case SQLITE_FLOAT:
                    value = SqlValue(sqlite3_column_double(stmt, i));
                    break;
                case SQLITE_TEXT:
                    {
                        int bytes = sqlite3_column_bytes(stmt, i);
                        const unsigned char* utf8 = sqlite3_column_text(stmt, i);
                        if (utf8 && bytes > 0) {
                            value = SqlValue(std::string(reinterpret_cast<const char*>(utf8), bytes));
                        } else {
                            value = SqlValue(std::string(""));
                        }
                    }
                    break;
                case SQLITE_NULL:
                    value = SqlValue(nullptr);
                    break;
                default:
                    {
                        int bytes = sqlite3_column_bytes(stmt, i);
                        const unsigned char* utf8 = sqlite3_column_text(stmt, i);
                        if (utf8 && bytes > 0) {
                            value = SqlValue(std::string(reinterpret_cast<const char*>(utf8), bytes));
                        } else {
                            value = SqlValue(std::string(""));
                        }
                    }
                    break;
            }

            row.push_back(value);
        }

        return row;
    }

void SQLiteDriver::closeCursor(const std::string& name) {
        auto it = cursors_.find(name);
        if (it != cursors_.end()) {
            if (it->second) {
                sqlite3_finalize(it->second);
            }
            cursors_.erase(it);
        }
    }

void SQLiteDriver::createOrMigrateTable(const std::string& tableName, const std::vector<TableColumn>& columns) {
        // Check if table exists
        std::string checkSql = "SELECT name FROM sqlite_master WHERE type='table' AND name=?";
        auto result = querySql(checkSql, {{"", tableName}});
        
        bool tableExists = !result.empty();
        
        if (!tableExists) {
            // Create new table
            std::stringstream createSql;
            createSql << "CREATE TABLE " << tableName << " (";
            
            for (size_t i = 0; i < columns.size(); ++i) {
                const auto& col = columns[i];
                if (i > 0) createSql << ", ";
                
                createSql << col.name << " ";
                
                // Map TRX types to SQLite types
                if (col.typeName == "INTEGER" || col.typeName == "INT") {
                    createSql << "INTEGER";
                } else if (col.typeName == "REAL" || col.typeName == "DOUBLE" || col.typeName == "FLOAT") {
                    createSql << "REAL";
                } else if (col.typeName == "TEXT" || col.typeName == "STRING") {
                    createSql << "TEXT";
                } else if (col.typeName == "BOOLEAN" || col.typeName == "BOOL") {
                    createSql << "INTEGER"; // SQLite uses INTEGER for booleans
                } else {
                    createSql << "TEXT"; // Default to TEXT for unknown types
                }
                
                if (col.isPrimaryKey) {
                    createSql << " PRIMARY KEY";
                }
                
                if (!col.isNullable) {
                    createSql << " NOT NULL";
                }
                
                if (col.defaultValue) {
                    createSql << " DEFAULT " << *col.defaultValue;
                }
            }
            
            createSql << ")";
            executeSql(createSql.str());
        } else {
            // TODO: Implement table migration logic
            // For now, just check if the table structure matches expectations
            // In a full implementation, this would:
            // 1. Get current table schema
            // 2. Compare with desired schema
            // 3. Generate ALTER TABLE statements as needed
            // 4. Handle data migration if necessary
        }
    }

void SQLiteDriver::beginTransaction() {
        executeSql("BEGIN TRANSACTION");
    }

bool SQLiteDriver::isInTransaction() {
    return sqlite3_get_autocommit(db_) == 0;
}

void SQLiteDriver::commitTransaction() {
        executeSql("COMMIT");
    }

void SQLiteDriver::rollbackTransaction() {
    executeSql("ROLLBACK");
}

std::vector<TableColumn> SQLiteDriver::getTableSchema(const std::string& tableName) {
    std::string sql = "PRAGMA table_info(" + tableName + ")";
    auto results = querySql(sql);
    
    std::vector<TableColumn> columns;
    for (const auto& row : results) {
        if (row.size() >= 6) {
            TableColumn col;
            col.name = row[1].asString(); // name
            std::string typeStr = row[2].asString(); // type
            
            // Map SQLite types to TRX types
            if (typeStr.find("INT") != std::string::npos) {
                col.typeName = "INTEGER";
            } else if (typeStr.find("CHAR") != std::string::npos || typeStr.find("TEXT") != std::string::npos) {
                col.typeName = "CHAR";
                // Extract length if present
                size_t parenPos = typeStr.find('(');
                if (parenPos != std::string::npos) {
                    size_t endPos = typeStr.find(')', parenPos);
                    if (endPos != std::string::npos) {
                        std::string lenStr = typeStr.substr(parenPos + 1, endPos - parenPos - 1);
                        try {
                            col.length = std::stoi(lenStr);
                        } catch (...) {}
                    }
                }
            } else if (typeStr.find("DECIMAL") != std::string::npos || typeStr.find("REAL") != std::string::npos) {
                col.typeName = "DECIMAL";
            } else if (typeStr.find("BOOL") != std::string::npos) {
                col.typeName = "BOOLEAN";
            } else {
                col.typeName = "CHAR"; // Default
            }
            
            col.isNullable = !row[3].asBool(); // notnull (0 = nullable, 1 = not null)
            col.isPrimaryKey = row[5].asNumber() > 0; // pk
            
            columns.push_back(col);
        }
    }
    
    return columns;
}

void SQLiteDriver::bindParameters(sqlite3_stmt* stmt, const std::vector<SqlParameter>& params) {
    for (size_t i = 0; i < params.size(); ++i) {
        const auto& param = params[i];
        int paramIndex = i + 1; // SQLite uses 1-based indexing for positional parameters
        
        if (param.value.isNull()) {
            sqlite3_bind_null(stmt, paramIndex);
        } else if (param.value.isBool()) {
            sqlite3_bind_int(stmt, paramIndex, param.value.asBool() ? 1 : 0);
        } else if (param.value.isNumber()) {
            double num = param.value.asNumber();
            if (num == std::floor(num) && num >= INT_MIN && num <= INT_MAX) {
                sqlite3_bind_int64(stmt, paramIndex, static_cast<long long>(num));
            } else {
                sqlite3_bind_double(stmt, paramIndex, num);
            }
        } else if (param.value.isString()) {
            const std::string& str = param.value.asString();
            sqlite3_bind_text(stmt, paramIndex, str.c_str(), str.size(), SQLITE_TRANSIENT);
        } else {
            // For complex types, convert to string
            std::stringstream ss;
            ss << param.value;
            std::string str = ss.str();
            sqlite3_bind_text(stmt, paramIndex, str.c_str(), str.size(), SQLITE_TRANSIENT);
        }
    }
}

} // namespace trx::runtime