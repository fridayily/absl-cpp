
#include <benchmark/benchmark.h>

static void BM_StringCreation(benchmark::State& state) {
  for (auto _ : state)
    std::string empty_string;
}
// Register the function as a benchmark
BENCHMARK(BM_StringCreation);

// Define another benchmark
static void BM_StringCopy(benchmark::State& state) {
  std::string x = "hello";
  for (auto _ : state)
    std::string copy(x);
}
BENCHMARK(BM_StringCopy);


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
BENCHMARK(BM_StringSplit);

static void BM_StringViewSplit(benchmark::State& state) {
    std::string_view input = "Hello,World,How,Are,You,Today";
    for (auto _ : state) {
        std::vector<std::string_view> result = split2(input, ",");
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_StringViewSplit);

BENCHMARK_MAIN();