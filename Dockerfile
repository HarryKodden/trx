# syntax=docker/dockerfile:1.6

FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=Etc/UTC

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential \
       cmake \
       git \
       flex \
       bison \
       ninja-build \
       libsqlite3-dev \
       libpq-dev \
       unixodbc-dev \
       odbc-postgresql \
       pkg-config \
       libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# Copy the rewrite project into the container build context
COPY . /workspace

# Configure and build the TRX rewrite using Ninja for faster feedback
RUN cmake -S . -B build \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build

# ---------------------------------------------------------------------------
# Runtime image that just ships the compiler binary
FROM ubuntu:24.04 AS runtime

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       libstdc++6 \
       libsqlite3-0 \
       libpq5 \
       libsqlite3-dev \
       cmake \
       ninja-build \
       build-essential \
       flex \
       bison \
       unixodbc \
       odbc-postgresql \
       pkg-config \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /workspace/build/src/trx_compiler /usr/local/bin/trx_compiler

WORKDIR /workspace

ENTRYPOINT ["/usr/local/bin/trx_compiler"]
CMD ["--help"]
