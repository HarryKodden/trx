# TRX - Transaction Processing Language

TRX is a domain-specific language for transaction processing, designed for building reliable database applications with explicit data structures, control flow, and error handling.

## Features

TRX provides a comprehensive set of features for transaction processing:

### Core Language Features
- **Strong Typing**: Record types, lists, and built-in types (INTEGER, CHAR, BOOLEAN, DECIMAL, etc.)
- **Automatic Type Definition**: Generate record types automatically from existing database table schemas using `TYPE name FROM TABLE table_name`
- **Variables and Constants**: Global and local variable declarations with type safety
- **Functions and Procedures**: Modular code organization with input/output parameters
- **Control Flow**: IF-ELSE, WHILE loops, and SWITCH statements with CASE/DEFAULT
- **Exception Handling**: TRY-CATCH blocks and THROW statements for error management
- **SQL Integration**: Direct SQL execution with host variables, cursors, and transaction management
- **Built-in Functions**: String manipulation (substr), list operations (len, append), logging (debug, info, error)
- **Modules**: INCLUDE statements for code organization across multiple files (allows duplicate identical type definitions)

### Runtime Features
- **Database Connectivity**: Support for SQLite, PostgreSQL, and ODBC connections
- **Transaction Management**: Automatic transaction handling with rollback on errors
- **REST API Server**: Built-in HTTP server for exposing procedures as web services
- **JSON Serialization**: Automatic conversion between TRX records and JSON

## Grammar Overview

TRX uses a Pascal-like syntax with SQL integration:

```trx
TYPE PERSON {
    ID INTEGER;
    NAME CHAR(64);
    AGE INTEGER;
}

TYPE EMPLOYEE FROM TABLE employee;

CONSTANT MAX_AGE 100;

FUNCTION calculate_bonus(PERSON): PERSON {
    IF input.AGE > 50 THEN {
        output.ID := input.ID;
        output.NAME := input.NAME;
        output.AGE := input.AGE + 10;
    } ELSE {
        output := input;
    }
    RETURN output;
}

PROCEDURE process_data() {
    VAR employees LIST(PERSON);
    
    EXEC_SQL SELECT ID, NAME, AGE FROM employees;
    
    WHILE SQLCODE = 0 {
        VAR emp PERSON;
        FETCH emp FROM employees_cursor;
        
        TRY {
            calculate_bonus(emp);
        } CATCH {
            THROW "Processing failed";
        }
    }
}
```

### Automatic Type Definition from Database Tables

TRX supports automatic generation of record types from existing database table schemas, eliminating code duplication:

```trx
-- Define a type automatically from the 'person' table schema
TYPE PERSON FROM TABLE person;

-- The type will have fields matching the database table:
-- - id: INTEGER (primary key)
-- - name: CHAR(64) (from VARCHAR(64))
-- - age: INTEGER
-- - active: BOOLEAN
-- - salary: DECIMAL(10,2)
```

This feature introspects the database at runtime and creates the appropriate TRX record type with correct field types, lengths, and constraints.

### Key Constructs
- **Declarations**: TYPE (manual or from table), CONSTANT, VAR, FUNCTION, PROCEDURE
- **Statements**: Assignment (:=), SQL execution, control flow, calls
- **Expressions**: Arithmetic, comparison, logical, function calls, field access
- **SQL**: EXEC_SQL, cursors (DECLARE, OPEN, FETCH, CLOSE), host variables (:var)

## Usage

### Command Line Options

```bash
# Compile and validate a TRX file
trx_compiler source.trx

# Execute a specific procedure
trx_compiler --procedure procedure_name source.trx

# Start REST API server
trx_compiler serve --port 8080 source.trx

# List available procedures in a file
trx_compiler list source.trx
```

### Run Options

- `trx_compiler <source.trx>`: Parse and validate the TRX source file
- `trx_compiler --procedure <name> <source.trx>`: Execute a specific procedure/function
- `trx_compiler serve [options] <sources...>`: Start HTTP server exposing procedures as REST endpoints
  - `--port <port>`: Server port (default: 8080)
  - `--procedure <name>`: Only expose specific procedure (default: all)
- `trx_compiler list <source.trx>`: List all procedures and functions defined in the file

### Database Connection Options

TRX supports multiple database backends:

- **SQLite** (default):
  - `--db-type sqlite`
  - `--db-connection <path>` (default: `:memory:`)
  - Environment: `DATABASE_TYPE=SQLITE`, `DATABASE_CONNECTION_STRING=<path>`

- **PostgreSQL**:
  - `--db-type postgresql`
  - `--db-connection "<host=localhost port=5432 user=user dbname=db>"`
  - Environment: `DATABASE_TYPE=POSTGRESQL`, `DATABASE_CONNECTION_STRING=<conn_str>`

- **ODBC**:
  - `--db-type odbc`
  - `--db-connection "<DSN=my_dsn;UID=user;PWD=pass>"`
  - Environment: `DATABASE_TYPE=ODBC`, `DATABASE_CONNECTION_STRING=<conn_str>`

### REST API Server

When running in serve mode, TRX automatically generates REST endpoints for each procedure:

```bash
# Start server
trx_compiler serve --port 8080 examples/sample.trx

# Call procedure via HTTP POST
curl -X POST http://localhost:8080/process_employee \
  -H "Content-Type: application/json" \
  -d '{"ID": 1, "NAME": "John", "AGE": 30}'
```

The server provides:
- Automatic JSON request/response handling
- Swagger/OpenAPI documentation at `/swagger.json`
- Error handling with proper HTTP status codes

## Building and Testing

### Prerequisites

- CMake 3.22+
- C++20 compiler (AppleClang 17+, GCC 11+, MSVC 2022+)
- Bison 3.7+ and Flex 2.6+
- SQLite3 development headers
- Optional: PostgreSQL and ODBC development headers

### Quick Build

```bash
# Configure
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Test
cd build && ctest --output-on-failure

# Run
./src/trx_compiler examples/sample.trx
```

### Docker Build

```bash
# Build development image
docker build --target builder -t trx-dev .

# Build runtime image
docker build -t trx-runtime .

# Run tests
docker run --rm -v "$PWD":/workspace trx-dev \
  bash -c 'cd /workspace && cmake -S . -B build -G Ninja && cmake --build build && cd build && ctest'

# Run compiler
docker run --rm -v "$PWD":/workspace trx-runtime /workspace/examples/sample.trx

# Start server
docker run --rm -v "$PWD":/workspace -p 8080:8080 trx-runtime serve /workspace/examples/sample.trx
```

## Examples

See the `examples/` directory for sample TRX programs:

- `sample.trx`: Basic record processing and SQL operations
- `init.trx`: Database initialization and automatic type definition from tables
- `cursor_example.trx`: Cursor operations for reading multiple records into JSON output
- `exception_test.trx`: Error handling examples
- `global_test.trx`: Global variables and functions
- `test_function.trx`: Function definitions and calls

## Architecture

TRX is built as a modular C++ project:

- **AST**: Immutable data structures for parsed code
- **Parsing**: Flex/Bison-based parser with semantic actions
- **Runtime**: Interpreter with symbol tables, database drivers, and execution engine
- **CLI**: Command-line interface and REST server
- **Diagnostics**: Structured error reporting

The design emphasizes testability, with comprehensive unit tests covering parsing, AST construction, and runtime execution.
