#include "trx/runtime/DatabaseDriver.h"
#include "trx/runtime/SQLiteDriver.h"
#include "trx/runtime/PostgreSQLDriver.h"
#include "trx/runtime/ODBCDriver.h"

namespace trx::runtime {

std::unique_ptr<DatabaseDriver> createDatabaseDriver(const DatabaseConfig& config) {
    switch (config.type) {
        case DatabaseType::SQLITE:
            return std::make_unique<SQLiteDriver>(config);

        case DatabaseType::POSTGRESQL:
            return std::make_unique<PostgreSQLDriver>(config);

        case DatabaseType::DB2:
            throw std::runtime_error("DB2 driver not yet implemented");

        case DatabaseType::ODBC:
            return std::make_unique<ODBCDriver>(config);

        default:
            throw std::runtime_error("Unknown database type");
    }
}

} // namespace trx::runtime