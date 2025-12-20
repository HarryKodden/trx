#include "trx/parsing/ParserDriver.h"

#include <filesystem>
#include <iostream>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: trx_compiler <source.trx>\n";
        return 1;
    }

    trx::parsing::ParserDriver driver;
    if (!driver.parseFile(std::filesystem::path{argv[1]})) {
        for (const auto &diagnostic : driver.diagnostics().messages()) {
            std::cerr << diagnostic.message << "\n";
        }
        return 1;
    }

    std::cout << "Parsed " << argv[1] << " successfully\n";
    return 0;
}
