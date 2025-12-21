#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace trx::cli {

struct ServeOptions {
    int port{8080};
    std::optional<std::string> procedure;
};

int runSwaggerServer(const std::filesystem::path &sourcePath, ServeOptions options);

} // namespace trx::cli
