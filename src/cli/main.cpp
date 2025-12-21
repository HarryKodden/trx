#include "SwaggerServer.h"
#include "trx/parsing/ParserDriver.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace {

void printUsage() {
    std::cerr << "Usage:\n";
    std::cerr << "  trx_compiler <source.trx>\n";
    std::cerr << "  trx_compiler serve [--port <port>] [--procedure <name>] <source.trx>\n";
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    bool serveMode = false;
    trx::cli::ServeOptions serveOptions;
    std::optional<std::filesystem::path> sourcePath;

    for (int index = 1; index < argc; ++index) {
        const std::string argument{argv[index]};
        if (argument == "-h" || argument == "--help") {
            printUsage();
            return 0;
        }
        if (argument == "serve" || argument == "--serve") {
            serveMode = true;
            continue;
        }
        if ((argument == "--port" || argument == "-p") && index + 1 < argc) {
            try {
                serveOptions.port = std::stoi(argv[++index]);
            } catch (const std::exception &) {
                std::cerr << "Invalid port value\n";
                return 1;
            }
            if (serveOptions.port <= 0 || serveOptions.port > 65535) {
                std::cerr << "Port must be between 1 and 65535\n";
                return 1;
            }
            continue;
        }
        if ((argument == "--procedure" || argument == "-r") && index + 1 < argc) {
            serveOptions.procedure = std::string{argv[++index]};
            continue;
        }
        if (!sourcePath) {
            sourcePath = std::filesystem::path{argument};
            continue;
        }

        std::cerr << "Unexpected argument: " << argument << "\n";
        return 1;
    }

    if (!sourcePath) {
        std::cerr << "Missing TRX source file\n";
        printUsage();
        return 1;
    }

    if (serveMode) {
        return trx::cli::runSwaggerServer(*sourcePath, serveOptions);
    }

    trx::parsing::ParserDriver driver;
    if (!driver.parseFile(*sourcePath)) {
        for (const auto &diagnostic : driver.diagnostics().messages()) {
            std::cerr << diagnostic.message << "\n";
        }
        return 1;
    }

    std::cout << "Parsed " << sourcePath->string() << " successfully\n";
    return 0;
}
