# An Effective Hybrid Local Search method for FJSP problem(HLS-FJSP)

HLS-FJSP is an effective hybrid local search method for FJSP problem. The algorithm adopts a two-phase optimization framework and  introduces a control parameter dynamically coordinates greedy heuristics, genetic operations, and tabu search, thereby effectively addressing critical issues including solution feasibility, diversity, and optimization efficiency through the collaborative optimization of initialization and local search phases.

完整的中文说明（问题定义、运行方法、数据格式和系统集成）见
[PROJECT_GUIDE.md](PROJECT_GUIDE.md)。

## Get Started

The proposed HLS-FJSP and all baselines are implemented in `C++`.

### Build Environment

The project has been tested with the following toolchain:
- **C++ Compilers**:
  - GCC/G++ 11.4.0
  - Clang 19.0.0
- **Build System**: CMake 3.22.1
- **OS**: Ubuntu 22.04.3 LTS

To verify your environment:
```bash
# Check compiler versions
g++ --version
clang++ --version

# Check CMake version
cmake --version
```

### Build

```shell
mkdir build && cd build
cmake ..
make
```

The executable will be generated as `production_scheduling` or similar.

### Usage

```shell
./production_scheduling <input_file> <search_mode_param> <random_search_strategy_param> <max_repeat_count> <max_iter_count> <population_size> <tabu_list_length> [algorithm_mode]
```

`algorithm_mode` is optional and defaults to `HLS`. Supported modes:

- `HLS`: the original hybrid search;
- `GS`: greedy search only;
- `GA`: genetic algorithm only;
- `TS`: tabu search only;
- `GSGA`: greedy search and genetic algorithm;
- `GSTS`: greedy search and tabu search;
- `GATS`: genetic algorithm and tabu search.

For `GSGA` and `GSTS`, `search_mode_param` is the probability of selecting the
second algorithm. For `GATS`, `random_search_strategy_param` is the probability
of selecting GA. For example:

```shell
./production_scheduling ../datasets/SmartLab_Challenge_2020/fjsp.brandimarte.Mk01.m6j10c3.txt 0.5 0.5 100 30 20 25 GS
```

This runs the `GS` ablation mode on the specified instance.

#### Output

- The program prints the total scheduling time to the console.

### Parameters

You can adjust search parameters in `main.cpp`:

- `search_mode_param`: Probability to choose random search vs. greedy search.
- `random_search_strategy_param`: Probability to choose genetic algorithm vs. tabu search in random search.
- `max_iter_count`: Maximum iterations per optimization round.
- `population_size`: Population size for genetic algorithm.
- `tabu_list_length`: Tabu list length for tabu search.
- `max_repeat_count`: Maximum allowed repeats of the best solution before stopping.
- `algorithm_mode`: Search mode used for the run; defaults to `HLS`.
