
#include <benchmark/benchmark.h>
#include <iostream>
#include <set>
#include <random>

static void BM_memcpy(benchmark::State& state) {
    char* src = new char[state.range(0)];
    char* dst = new char[state.range(0)];
    memset(src, 'x', state.range(0));
    for (auto _ : state)
        memcpy(dst, src, state.range(0));
    state.SetBytesProcessed(int64_t(state.iterations()) *
                            int64_t(state.range(0)));
    delete[] src;
    delete[] dst;
}
BENCHMARK(BM_memcpy)->Arg(8)->Arg(64)->Arg(512)->Arg(4<<10)->Arg(8<<10);


BENCHMARK(BM_memcpy)->Range(8, 8<<10);

BENCHMARK(BM_memcpy)->RangeMultiplier(2)->Range(8, 8<<10);


static void BM_DenseRange(benchmark::State& state) {
    for(auto _ : state) {
        std::vector<int> v(state.range(0), state.range(0));
        auto data = v.data();
        benchmark::DoNotOptimize(data);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_DenseRange)->DenseRange(0, 1024, 128);


// 生成一个包含 n 个随机整数的集合
std::set<int> ConstructRandomSet(int n) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 1000000);

    std::set<int> data;
    while (data.size() < n) {
        data.insert(dis(gen));
    }
    return data;
}

// 生成一个随机整数
int RandomNumber() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 1000000);
    return dis(gen);
}

static void BM_SetInsert(benchmark::State& state) {
    std::set<int> data;
    for (auto _ : state) {
        state.PauseTiming();
        data = ConstructRandomSet(state.range(0));
        state.ResumeTiming();
        for (int j = 0; j < state.range(1); ++j)
            data.insert(RandomNumber());
    }
}
BENCHMARK(BM_SetInsert)
    ->Args({1<<10, 128})
    ->Args({2<<10, 128})
    ->Args({4<<10, 128})
    ->Args({8<<10, 128})
    ->Args({1<<10, 512})
    ->Args({2<<10, 512})
    ->Args({4<<10, 512})
    ->Args({8<<10, 512});

//
// template <class... Args>
// void BM_takes_args(benchmark::State& state, Args&&... args) {
//     // 将参数打包成一个元组
//     auto args_tuple = std::make_tuple(std::forward<Args>(args)...);
//
//     // 基准测试循环
//     for (auto _ : state) {
//         // 使用 std::apply 来展开元组并调用一个 lambda 函数
//         std::apply([&state, &args_tuple](auto&&... args) {
//             // 这里可以添加你的基准测试代码
//             // 示例：打印元组中的参数
//             std::cout << std::get<0>(args_tuple) << ": " << std::get<1>(args_tuple) << '\n';
//             // 其他操作...
//         }, args_tuple);
//
//         // 防止编译器优化掉循环体
//         benchmark::DoNotOptimize(args_tuple);
//     }
// }
// // Registers a benchmark named "BM_takes_args/int_string_test" that passes
// // the specified values to `args`.
// BENCHMARK_CAPTURE(BM_takes_args, int_string_test, 42, std::string("abc"));
//
// // Registers the same benchmark "BM_takes_args/int_test" that passes
// // the specified values to `args`.
// BENCHMARK_CAPTURE(BM_takes_args, int_test, 42, 43);


BENCHMARK_MAIN();