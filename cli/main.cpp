#include "cli_impl.h"

#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    return menos::cli::run(args, std::cout, std::cerr);
}
