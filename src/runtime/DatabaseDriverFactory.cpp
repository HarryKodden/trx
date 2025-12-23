#include "trx/runtime/DatabaseDriver.h"
#include "trx/runtime/SQLiteDriver.h"
#ifdef HAVE_POSTGRESQL
#include "trx/runtime/PostgreSQLDriver.h"
#endif
#ifdef HAVE_ODBC
#include "trx/runtime/ODBCDriver.h"
#endif

namespace trx::runtime {

std::unique_ptr<DatabaseDriver> createDatabaseDriver(const DatabaseConfig& config) {
    switch (config.type) {
        case DatabaseType::SQLITE:
            return std::make_unique<SQLiteDriver>(config);

        case DatabaseType::POSTGRESQL:
#ifdef HAVE_POSTGRESQL
            return std::make_unique<PostgreSQLDriver>(config);
#else
            throw std::runtime_error("PostgreSQL support not compiled in");
#endif

        case DatabaseType::DB2:
            throw std::runtime_error("DB2 driver not yet implemented");

        case DatabaseType::ODBC:
#ifdef HAVE_ODBC
            return std::make_unique<ODBCDriver>(config);
#else
            throw std::runtime_error("ODBC support not compiled in");
#endif

        default:
            throw std::runtime_error("Unknown database type");
    }
}

} // namespace trx::runtime