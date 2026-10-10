// Shared main() for every benchmark binary under benchmarks/: prints the
// environment info docs/BUILD_PLAN.md Phase 3, 3.9 requires to reproduce
// a result before Google Benchmark's own report, instead of each
// benchmark file using the BENCHMARK_MAIN() macro.

#include <benchmark/benchmark.h>

#include <iostream>

#include "menos_benchmark_environment.h"

int main(int argc, char** argv) {
    std::cout << menos_benchmark::describe_environment() << "\n";
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
