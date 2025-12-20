# TRX Rewrite

This directory hosts a ground-up rewrite of the TRX transaction toolkit with a focus on composable modules, modern tooling, and explicit data structures.

## Goals

- Replace the monolithic legacy build with an incremental, testable C++ project.
- Keep the original parsing grammar semantics while organising supporting code into cohesive libraries.
- Provide clear separation between the parser, semantic actions, runtime services, and persistence layers.

## Layout

```
rewrite/
  CMakeLists.txt        # Top-level build definition
  cmake/                # CMake helper modules
  include/              # Public headers grouped by domain
    trx/
      ast/
      diagnostics/
      parsing/
      runtime/
  src/
    ast/
    diagnostics/
    parsing/
    runtime/
    cli/
  tools/
    grammar/
      trx_parser.y      # Bison grammar (C++ skeleton)
      trx_lexer.l       # Flex lexer
```

Each submodule has a dedicated responsibility:

- `ast`: Immutable data structures that capture declarations, statements, and expressions produced by the parser.
- `parsing`: Glue code between Flex/Bison and the AST builder.
- `runtime`: Minimal services needed during compilation (symbol tables, memory management, include resolution).
- `diagnostics`: Structured error and warning reporting utilities.
- `cli`: Entry points for command line tools built on the library.

## Next Steps

1. Implement the foundational diagnostics and symbol table services.
2. Port the parser actions to emit AST constructs instead of invoking global procedural APIs.
3. Reintroduce database/code generation features as separate libraries once the parser layer is stable.

## Tooling Requirements

The build expects modern toolchain components:

- CMake 3.22+
- A C++20 capable compiler (tested with AppleClang 17)
- Bison 3.7+ and Flex 2.6+ (`brew install bison flex` on macOS). Ensure `/usr/local/opt/bison/bin` occurs in `PATH` before configuring so CMake picks up the newer binaries.

### Containerised Build

To avoid managing host-side toolchain updates you can build the project inside the provided Docker image:

```
docker build -t trx-rewrite:latest rewrite
docker run --rm -v "$(pwd)":/workspace trx-rewrite:latest rewrite/examples/sample.trx
```

The resulting container exposes the `trx_compiler` binary as the entrypoint, so passing a path inside the bind-mounted workspace allows you to compile sources without installing dependencies locally.
# trx
