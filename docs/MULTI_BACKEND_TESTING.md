# Multi-Backend Testing

The TRX test suite now supports testing against multiple database backends: SQLite, PostgreSQL, and ODBC.

## Usage

### Test with default (SQLite only)
```bash
make test
```

### Test with PostgreSQL
```bash
make test-postgres
```

### Test with ODBC
```bash
make test-odbc
```

### Test with all configured backends
```bash
make test-all
```

## Configuration

Database backends are controlled via environment variables:

- `TEST_DB_BACKENDS`: Comma-separated list of backends to test (`sqlite`, `postgresql`, `odbc`, or `all`)
- `POSTGRES_HOST`: PostgreSQL server hostname (default: `localhost`)
- `POSTGRES_PORT`: PostgreSQL server port (default: `5432`)
- `POSTGRES_DB`: PostgreSQL database name (default: `trx`)
- `POSTGRES_USER`: PostgreSQL username (default: `trx`)
- `POSTGRES_PASSWORD`: PostgreSQL password (default: `password`)
- `ODBC_CONNECTION_STRING`: ODBC connection string

## Implementation

Tests use the `getTestDatabaseBackends()` helper from `TestUtils.h` which returns a list of configured backends. Each test that requires database access iterates over all backends:

```cpp
const auto backends = getTestDatabaseBackends();
for (const auto& backend : backends) {
    std::cout << "\n=== Testing with " << backend.name << " ===\n";
    auto dbDriver = createTestDatabaseDriver(backend);
    trx::runtime::Interpreter interpreter(module, std::move(dbDriver));
    // ... run tests ...
}
```

## Current Status

### Working
- ✅ All tests pass with SQLite (default, 100%)
- ✅ 13/16 tests pass with PostgreSQL (81%)
- ✅ 13/16 tests pass with ODBC (81%)
- ✅ Most non-database tests are backend-agnostic

### Known Issues with PostgreSQL and ODBC
Both PostgreSQL (native driver) and ODBC exhibit the same behavior:

- ⚠️ **CursorTest**: Aborts on transaction errors
- ⚠️ **ComprehensiveDatabaseTest**: Aborts on transaction errors
- ⚠️ **SqlPatternsTest**: Returns incomplete results
  
**Root cause**: PostgreSQL and ODBC abort transactions on errors (e.g., table already exists), while SQLite allows continued execution after errors.

## Fixing PostgreSQL Issues

To make tests fully compatible with PostgreSQL:

1. **Use IF NOT EXISTS for table creation:**
   ```sql
   CREATE TABLE IF NOT EXISTS my_table (...)
   ```

2. **Handle unique constraint violations:**
   ```sql
   INSERT ... ON CONFLICT DO NOTHING
   ```

3. **Proper transaction management:**
   - COMMIT after successful operations
   - ROLLBACK and retry on errors
   - Or use separate transactions for setup vs test operations

4. **Drop tables before tests:**
   ```sql
   DROP TABLE IF EXISTS my_table;
   CREATE TABLE my_table (...);
   ```

## Benefits

Testing against multiple backends ensures:
- **Correctness**: Catches SQL compatibility issues
- **Portability**: Verifies TRX code works across different databases
- **Real-world readiness**: Tests against production-like environments (PostgreSQL)
- **Bug detection**: Different backends have different behaviors (transaction handling, data types, error handling)

## Next Steps

1. Fix ComprehensiveDatabaseTest to use `IF NOT EXISTS` and proper error handling
2. Fix SqlPatternsTest similarly
3. Add ODBC backend testing (requires ODBC driver setup)
4. Consider adding MySQL/MariaDB backend support
5. Add backend-specific integration tests for features like:
   - Cursor positioning (UPDATE WHERE CURRENT OF)
   - Large result sets
   - Complex joins
   - Transaction isolation levels

```
╔══════════════════════════════════════════════════════════════╗
║           TRX Multi-Backend Test Results                     ║
╠══════════════════════════════════════════════════════════════╣
║ Backend      │ Status    │ Pass Rate │ Notes                 ║
╠══════════════════════════════════════════════════════════════╣
║ SQLite       │ ✅ PASS   │ 16/16     │ Default, all tests OK ║
║ PostgreSQL   │ ⚠️  WARN  │ 13/16     │ Transaction handling  ║
║ ODBC (PgSQL) │ ⚠️  WARN  │ 13/16     │ Same as PostgreSQL    ║
╠══════════════════════════════════════════════════════════════╣
║ Overall: Multi-backend infrastructure working!               ║
║ Issues: 3 tests need IF NOT EXISTS for PostgreSQL/ODBC      ║
╚══════════════════════════════════════════════════════════════╝

Failing tests on PostgreSQL/ODBC:
  • CursorTest (aborts on duplicate table)
  • ComprehensiveDatabaseTest (aborts on duplicate table)
  • SqlPatternsTest (incomplete results due to transaction abort)

Commands:
  make test            - SQLite only (default)
  make test-postgres   - PostgreSQL native driver
  make test-odbc       - ODBC driver with PostgreSQL
  make test-all        - All three backends

```