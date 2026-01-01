#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <thread>

#include "trx/runtime/DatabaseDriver.h"

namespace trx::cli {

struct ServeOptions {
    int port{8080};
    std::optional<std::string> procedure;
    trx::runtime::DatabaseConfig dbConfig;
    size_t threadCount{std::thread::hardware_concurrency()};
};

int runServer(const std::vector<std::filesystem::path> &sourcePaths, ServeOptions options);

} // namespace trx::cli
