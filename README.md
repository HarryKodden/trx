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

Run the following commands from the `rewrite/` directory. Build both the runtime image (ships the `trx_compiler` entrypoint) and a separate toolchain image with CMake, Ninja, Flex, and Bison installed:

```
docker build -t trx-rewrite .
docker build --target builder -t trx-rewrite-dev .
```

#### How to build

Configure the project and compile it inside the toolchain image. This writes the artefacts to `build/` in your working tree:

```
docker run --rm -v "$PWD":/workspace --entrypoint bash trx-rewrite-dev \
  -lc 'cmake -S /workspace -B /workspace/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && \
       cmake --build /workspace/build'
```

#### How to test

Re-use the configured build directory and drive CTest from within the same image:

```
docker run --rm -v "$PWD":/workspace --entrypoint bash trx-rewrite-dev \
  -lc 'cd /workspace/build && ctest --output-on-failure'
```

#### How to run

Invoke the runtime image (whose entrypoint is `trx_compiler`) and pass it a TRX source file inside the mounted workspace:

```
docker run --rm -v "$PWD":/workspace trx-rewrite /workspace/examples/sample.trx
```

The compiler reports diagnostics or success without requiring any host-side dependencies beyond Docker.

## Continuous Integration

Pushes and pull requests targeting `main` trigger a GitHub Actions workflow that:

- Builds with warnings promoted to errors to provide lightweight linting.
- Builds and tests the project via CTest on an Ubuntu runner.
- Builds both the runtime and toolchain Docker images to ensure container definitions stay valid.
# trx
