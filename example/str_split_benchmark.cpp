
#include <benchmark/benchmark.h>


std::vector<std::string> split1(const std::string& s, const std::string& delimiters = " ") {
    std::vector<std::string> tokens;
    std::size_t lastPos = s.find_first_not_of(delimiters, 0);
    std::size_t pos = s.find_first_of(delimiters, lastPos);
    while (pos != std::string::npos || lastPos != std::string::npos) {
        tokens.push_back(s.substr(lastPos, pos - lastPos));
        lastPos = s.find_first_not_of(delimiters, pos);
        pos = s.find_first_of(delimiters, lastPos);
    }
    return tokens;
}

std::vector<std::string_view> split2(std::string_view s, std::string_view delimiters = " ") {
    std::vector<std::string_view> tokens;
    std::size_t lastPos = s.find_first_not_of(delimiters, 0);
    std::size_t pos = s.find_first_of(delimiters, lastPos);
    while (pos != std::string_view::npos || lastPos != std::string_view::npos) {
        tokens.push_back(s.substr(lastPos, pos - lastPos));
        lastPos = s.find_first_not_of(delimiters, pos);
        pos = s.find_first_of(delimiters, lastPos);
    }
    return tokens;
}


static void BM_StringSplit(benchmark::State& state) {
    std::string input = "Hello,World,How,Are,You,Today";
    for (auto _ : state) {
        std::vector<std::string> result = split1(input, ",");
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_StringSplit)->Iterations(100000);

static void BM_StringViewSplit(benchmark::State& state) {
    std::string_view input = "Hello,World,How,Are,You,Today";
    for (auto _ : state) {
        std::vector<std::string_view> result = split2(input, ",");
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_StringViewSplit)->Iterations(100000);

BENCHMARK_MAIN();