# HLS-FJSP 项目说明与集成指南

本文档以当前目录中的 `main.cpp`、`DataProc.cpp`、`GreedySearch.cpp`、
`RandomSearch.cpp`、`TabuSearch.cpp` 和相关头文件为准，说明项目解决的问题、
运行方式、数据格式以及接入其他系统时需要遵守的接口约定。

## 1. 问题背景

柔性作业车间调度问题（Flexible Job Shop Scheduling Problem，FJSP）包含若干
工件（job）和机器（machine）。每个工件由一组有固定先后顺序的工序
（operation）组成；每道工序可以选择多台兼容机器，不同机器上的加工时间可以
不同。求解器需要同时确定：

1. 每道工序分配到哪台兼容机器；
2. 每台机器上的工序加工顺序。

当前实现使用以下约束：

- 每道工序只能选择一台兼容机器；
- 同一工件的工序必须按输入顺序执行；
- 同一台机器同一时刻最多加工一道工序；
- 工序不可抢占，加工开始后连续执行到结束；
- 所有工件和机器在时刻 0 可用，加工时间为非负整数。

优化目标是最小化最大完工时间（makespan）：

```text
Cmax = max(C_j)
```

其中 `C_j` 是工件 `j` 最后一道工序的完成时间。`Machine` 和 `Schedule` 中虽然
保留了故障率字段，当前输入解析器不会读取故障率，搜索算法也只按 makespan
接受和比较解。因此故障率不是当前优化目标。

## 2. 方法概述

HLS-FJSP 先生成一个完整可行调度，再组合三种搜索方法继续降低 makespan：

- **Greedy Search（GS）**：围绕工序交换和跨机器移动构造邻域，保留更优候选；
- **Genetic Algorithm（GA）**：使用机器选择编码（MS）和工序排序编码（OS）进行
  种群初始化、交叉和变异；
- **Tabu Search（TS）**：围绕关键工序生成移动和交换邻域，用调度签名维护禁忌表。

初始解按工件顺序遍历工序，将工序分配给当前已分配工序数较少的兼容机器；
故障率仅作为并列条件，但当前默认值均为 0。

每个完整调度由两类有向边表示：

- 工件边：同一工件相邻工序之间的先后关系；
- 机器边：同一机器上相邻工序之间的执行顺序。

`ScheduleItemsToGraph` 构造带虚拟 `start`、`end` 节点的有向图，边权为源工序
在已选机器上的加工时间。`CalculateTotalTime` 对图执行拓扑排序和最长路计算，
`end` 节点的最早时间即 makespan；如果图中有环，候选调度不可行。

## 3. 代码结构

| 文件 | 作用 |
| --- | --- |
| `main.cpp` | 命令行入口、参数解析、搜索模式调度和结果输出 |
| `DataProc.*` | 实例解析、初始解、调度图构造、makespan 和停止条件 |
| `Schedule.*` | 调度结构、机器序列、图、开始时间和关键工序 |
| `Job.*` | 工件、工序及其可选机器/加工时间 |
| `Machine.*` | 机器信息和已分配工序计数 |
| `Order.*` | 将输入工件转换为待加工工件列表 |
| `GreedySearch.*` | 贪心交换和移动邻域 |
| `RandomSearch.*` | GA、OS/MS 编解码及 HLS 中的 GA/TS 分派 |
| `TabuSearch.*` | 禁忌搜索及关键工序邻域 |
| `Experiment.*` | FIFO/MOPNR/MWKR 与 EET/SPT 组合规则基线 |
| `run_brandimarte.sh` | Brandimarte 10 个实例各运行 10 次 |
| `run_ablation.sh` | GS、GA、TS 及两两组合的消融实验 |
| `run_barnes_experiments.py` | Barnes 批量实验和资源自适应并发 |

## 4. 构建项目

### 4.1 环境要求

- Linux；
- 支持 C++20 的编译器，项目已使用 GCC/G++ 11.4；
- CMake 3.22 或更高版本。

项目没有第三方 C++ 库依赖。`CMakeLists.txt` 对 GNU 运行库设置了
`-static-libgcc -static-libstdc++`；若使用不支持这些参数的工具链，需要移除或按
平台调整该设置。

### 4.2 编译命令

从项目根目录执行：

```bash
cd /home/luopw24/HLS_FJSP-master
cmake -S . -B build
cmake --build build -j
```

生成的程序为：

```text
/home/luopw24/HLS_FJSP-master/build/production_scheduling
```

## 5. 运行项目

### 5.1 单实例运行

命令格式：

```bash
./build/production_scheduling \
  <input_file> \
  <search_mode_param> \
  <random_search_strategy_param> \
  <max_repeat_count> \
  <max_iter_count> \
  <population_size> \
  <tabu_list_length> \
  [algorithm_mode]
```

示例：

```bash
./build/production_scheduling \
  datasets/SmartLab_Challenge_2020/fjsp.brandimarte.Mk01.m6j10c3.txt \
  0.5 0.5 100 30 20 25 HLS
```

`algorithm_mode` 省略时默认为 `HLS`。支持的模式如下：

| 模式 | 执行方法 | 概率参数的含义 |
| --- | --- | --- |
| `HLS` | Greedy + GA + Tabu | `search_mode_param` 为进入 RandomSearch 的概率；RandomSearch 前半停滞阶段固定用 GA，之后 `random_search_strategy_param` 为选择 GA 的概率，否则用 TS |
| `GS` | 仅 Greedy | 两个概率参数被忽略 |
| `GA` | 仅 GA | 两个概率参数被忽略 |
| `TS` | 仅 Tabu | 两个概率参数被忽略 |
| `GSGA` | Greedy + GA | `search_mode_param` 为选择 GA 的概率 |
| `GSTS` | Greedy + Tabu | `search_mode_param` 为选择 TS 的概率 |
| `GATS` | GA + Tabu | `random_search_strategy_param` 为选择 GA 的概率 |

参数建议：

| 参数 | 含义 | 约束/建议 |
| --- | --- | --- |
| `search_mode_param` | 外层方法选择概率 | 使用 `[0,1]` |
| `random_search_strategy_param` | GA/TS 选择概率 | 使用 `[0,1]` |
| `max_repeat_count` | 最优 makespan 连续未改善上限 | 非负整数 |
| `max_iter_count` | 单次 Greedy/GA/TS 的迭代或候选预算 | 正整数 |
| `population_size` | GA 种群大小 | 正整数，建议不小于 4 |
| `tabu_list_length` | 禁忌表容量 | 正整数 |

程序当前没有完整的参数范围检查，接入方应在启动前验证参数。停止条件还包含源码
中的硬上限：搜索阶段超过 3600 秒、外层循环次数超过 1200，或未改善次数超过
`max_repeat_count` 时停止。计时从初始解生成之后开始。

### 5.2 输出格式

运行过程中会输出参数、初始 makespan、各搜索方法耗时和当前最好值。日志中的
`Total time:` 表示 makespan，不是墙钟时间。

正常结束时，最后两个非空行是两个不带标签的整数：

```text
<final_makespan>
<search_elapsed_seconds>
```

例如：

```text
140
0
```

现有批处理脚本依赖这个约定提取结果。CLI 默认不输出每道工序的机器、开始时间
和结束时间；需要完整调度时，应采用第 8.2 节的源码级接口或扩展结构化输出。

### 5.3 批量运行

Brandimarte 的 10 个实例各运行 10 次：

```bash
./run_brandimarte.sh
```

默认结果写入 `logs/results.csv`，每次运行的完整日志写入 `logs/`。常用环境变量：

```bash
MAX_CONCURRENT=5 \
ALGORITHM_MODE=HLS \
SEARCH_MODE_PARAM=0.5 \
RANDOM_SEARCH_STRATEGY_PARAM=0.5 \
MAX_REPEAT_COUNT=100 \
MAX_ITER_COUNT=30 \
POPULATION_SIZE=20 \
TABU_LIST_LENGTH=25 \
./run_brandimarte.sh
```

运行全部六种消融模式，每种为 10 个实例乘 10 次：

```bash
./run_ablation.sh
```

Barnes 自适应并发脚本：

```bash
python3 run_barnes_experiments.py --runs 10 --max-parallel 20
```

该脚本依赖相邻项目 `/home/luopw24/HLS/run_adaptive_brandimarte.py` 提供资源调度
工具；可通过 `HLS_ROOT` 指向其他包含该模块的目录。因此它不是完全独立的入口。

## 6. 数据集格式

### 6.1 语法

输入是以空白字符分隔的整数文本。首行格式：

```text
<job_count> <machine_count> <metadata>
```

当前解析器读取但不使用第三个整数，仍必须提供它。每个工件随后按以下格式描述：

```text
<operation_count>
  <candidate_machine_count_of_op_0>
    <machine_id> <processing_time> ...
  <candidate_machine_count_of_op_1>
    <machine_id> <processing_time> ...
  ...
```

通常每个工件写成一行，但解析器按空白 token 读取，换行本身没有语义。

以下是一个包含 2 个工件、3 台机器的最小示例：

```text
2 3 2
2  2 0 3 2 5  1 1 4
1  2 1 2 2 3
```

解释：

- 工件 0 有两道工序；工序 0 可在机器 0/2 上加工，时间分别为 3/5；工序 1
  只能在机器 1 上加工，时间为 4；
- 工件 1 有一道工序，可在机器 1/2 上加工，时间分别为 2/3。

### 6.2 编号和数值约束

当前实现有以下严格约定：

- 工件 ID 由行序隐式生成，范围为 `0..job_count-1`；
- 工序 ID 由同一工件中的出现顺序隐式生成，从 0 开始；
- **输入机器 ID 必须从 0 开始**，范围为 `0..machine_count-1`；
- 加工时间应为正整数；
- 每道工序至少应有一台兼容机器；
- 首行第三项必须能按整数读取，即使其值不会参与求解。

一些公开 FJSP 文件使用从 1 开始的机器编号，或者使用小数形式的第三个统计字段，
不能直接交给当前解析器。导入前应将机器 ID 全部减 1，并把第三项转换为整数占位
值。仓库中的 `datasets/SmartLab_Challenge_2020` 使用当前程序所需的 0-based 格式；
其他数据子目录即使随仓库提供，也应先检查编号格式，不能仅凭 `.fjs` 扩展名判断
兼容性。

### 6.3 内置数据集

主要数据目录包括：

- `datasets/SmartLab_Challenge_2020`：Brandimarte、Barnes、Dauzere、Hurink 等；
- `datasets/Brandimarte_Data/Text`；
- `datasets/Kacem_Data`；
- `datasets/fjsp-instances-main`：多个公开 FJSP family。

正式批处理脚本使用 `SmartLab_Challenge_2020`。接入新的数据集时建议先执行小预算
单实例测试，并检查最终 makespan 为正整数、进程退出码为 0。

## 7. 核心数据结构

`Job` 保存工件 ID、工序数以及每道工序的 `Process_item`：

```cpp
struct Process_item {
    int process_id;
    int machine_id;
    int process_time;
};
```

`Schedule` 以每台机器的有序工序列表表示解：

```cpp
struct Schedule_process {
    int job_id;
    int process_id;
};

struct Schedule_item {
    int machine_id;
    int process_count;
    std::vector<Schedule_process> schedule_process;
};
```

除此之外，`Schedule` 还保存调度图、图节点名、每个节点的最早开始时间和最终
makespan。`get_schedule_items()` 返回机器序列；`get_processList()` 与
`get_start_time()` 使用相同索引，其中包含首尾虚拟节点。

## 8. 接入其他系统

### 8.1 推荐方式：作为独立 CLI 求解器

这是当前最稳定、改动最少的接入方式：

1. 上游系统把实例转换为第 6 节格式并写入独立临时文件；
2. 使用参数数组启动 `build/production_scheduling`，避免拼接 shell 命令；
3. 设置略大于 3600 秒的外部超时，并捕获 stdout、stderr 和退出码；
4. 仅在退出码为 0 时，从最后两个非空纯数字行读取 makespan 和搜索耗时；
5. 将原始输入、参数、完整日志和结果一起保存，便于复现。

建议上层统一成如下结果对象：

```json
{
  "status": "ok",
  "objective": "makespan",
  "makespan": 140,
  "search_seconds": 0,
  "algorithm_mode": "HLS",
  "parameters": {
    "search_mode_param": 0.5,
    "random_search_strategy_param": 0.5,
    "max_repeat_count": 100,
    "max_iter_count": 30,
    "population_size": 20,
    "tabu_list_length": 25
  }
}
```

当前随机数来自 `time(nullptr)`、`std::rand()` 和 `std::random_device`，没有命令行
seed 参数。同一输入和参数不保证得到相同结果。需要严格复现时，应先在代码中统一
随机数引擎并新增显式 seed 参数。

### 8.2 C++ 源码级接入

如果调用方需要完整调度而非只有 makespan，可将除 `main.cpp` 外的源文件编译为
静态库，并调用现有函数：

```cpp
std::vector<Machine> machines;
std::vector<Job> jobs;
std::vector<Order> orders;
std::vector<std::string> job_list;

Init(machines, jobs, orders, input_path);
OrderToJobList(orders, job_list);

Schedule current = GenerateInitialSolution(job_list, jobs, machines);
Schedule improved = GreedySearch(current, jobs, job_list, 30, machines);
int makespan = CalculateTotalTime(improved);
```

GA 和 Tabu 的入口分别为：

```cpp
Schedule ga = AggressiveSearch(
    current, jobs, machines, job_list, population_size, max_iter_count);

Schedule ts = ConservativeSearch(
    current, jobs, job_list, tabu_list_length, max_iter_count, machines);
```

上述调用只展示单个方法。若要获得与 CLI 的 `HLS` 完全相同的行为，需要把
`main.cpp` 中的外层方法选择、失败回退、全局最好维护和停止条件一起抽取。

生产级接入建议新增稳定接口，而不是让业务代码直接复制 `main.cpp`：

```cpp
struct SolverConfig {
    double search_mode_param;
    double random_search_strategy_param;
    int max_repeat_count;
    int max_iter_count;
    int population_size;
    int tabu_list_length;
    std::string algorithm_mode;
    int time_limit_seconds;
    std::uint64_t seed;
};

struct SolverResult {
    Schedule schedule;
    int makespan;
    int elapsed_seconds;
};

SolverResult SolveFjsp(const FjspInstance &instance, const SolverConfig &config);
```

同时将 CMake 目标拆成 `hls_fjsp` 静态库和 `production_scheduling` CLI 两部分。

### 8.3 从 Schedule 导出完整排程

完整结果可以由 `Schedule` 还原：

1. 遍历 `get_schedule_items()` 得到每台机器上的工序顺序；
2. 通过 `(job_id, process_id)` 在 `get_processList()` 中找到节点索引；
3. 用相同索引读取 `get_start_time()`；
4. 调用 `GetProcessTime(jobs, machine_id, job_id, process_id)` 得到持续时间；
5. `end_time = start_time + process_time`。

建议结构化导出以下字段：

```text
job_id, process_id, machine_id, start_time, process_time, end_time
```

### 8.4 并发和可靠性注意事项

- `Init` 会临时把全局 `std::cin` 的缓冲区指向输入文件，不适合在同一进程内并发
  调用；服务化时应改成接收 `std::istream&` 或直接解析 `FjspInstance`。
- 搜索使用全局/线程局部随机数和可变的 `Machine` 计数。不要在多个线程间共享
  `machines`、`Schedule` 或搜索状态。
- 现有 CLI 一次只处理一个实例。并发批量求解应使用独立进程，现有脚本也是这样
  实现的。
- CLI 不提供结构化错误对象。上层必须同时检查退出码、超时、最后两行格式和
  makespan 是否为正数。
- `Init` 对格式错误的输入缺少完整异常报告。进入求解器前应验证 token 数、机器
  编号、加工时间、工序数和候选机器数。
- 代码中的 3600 秒与 1200 轮停止限制是硬编码。外部时限若需动态配置，应先改为
  `SolverConfig` 参数。

## 9. 接入前检查清单

- 输入机器编号已经转换为 0-based；
- 首行包含三个整数；
- 每道工序至少有一个合法机器和正加工时间；
- 概率参数位于 `[0,1]`，其余搜索参数满足预期范围；
- 使用 `build/production_scheduling` 而非假设可执行文件位于项目根目录；
- 保存 stdout/stderr，并按最后两个非空整数行解析结果；
- 需要完整调度时使用源码接口或新增 JSON 输出，不能只依赖当前 CLI；
- 需要复现实验时先增加显式 seed；
- 进程级并发，不在同一进程内并发调用当前 `Init`。

