#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <optional>

#include "trx/runtime/DatabaseDriver.h"

namespace trx::cli {

struct ServeOptions {
    int port{8080};
    std::optional<std::string> procedure;
    trx::runtime::DatabaseConfig dbConfig;
};

int runSwaggerServer(const std::filesystem::path &sourcePath, ServeOptions options);

} // namespace trx::cli
