#include <algorithm>
#include <iostream>

#include "DataProc.h"
#include "GreedySearch.h"
#include "RandomSearch.h"
#include "Schedule.h"
#include "Job.h"
#include "Machine.h"
#include "Order.h"
#include "Experiment.h"
#include <string.h>

using namespace std;

int main(int argc, char *argv[]) {
    if (argc < 8 || argc > 9) {
        std::cerr << "Usage: " << argv[0]
                  << " <input_file> <search_mode_param>"
                  << " <random_search_strategy_param> <max_repeat_count>"
                  << " <max_iter_count> <population_size> <tabu_list_length>"
                  << " [algorithm_mode]" << std::endl;
        std::cerr << "algorithm_mode: HLS, GS, GA, TS, GSGA, GSTS, or GATS"
                  << std::endl;
        return 1;
    }

    auto machines = std::vector<Machine>();      // 机器列表
    auto jobs = std::vector<Job>();              // 工件列表
    auto orders = std::vector<Order>();          // 订单列表
    std::string file_path = argc > 1 ? argv[1] : "/data/luopw/production_scheduling/input.txt"; // 运行命令示例：./main input.txt
    // cout << "file_path: " << file_path << endl;
    Init(machines, jobs, orders, file_path);    // 初始化机器、工件、订单
    auto jobList = std::vector<std::string>();   // 用于记录每个工件列表，根据订单生成
    OrderToJobList(orders, jobList);         // 将订单转换为待加工的工件列表
    Schedule input_schedule;                    // 输入的调度方案
    Schedule output_schedule;                   // 输出的调度方案

    // ======parameters========
    // double search_mode_param = 0.5;  // 搜索模式参数0~1，配置选择随机或者贪心
    // double random_search_strategy_param = 0.5;  // 搜索模式参数，配置选择遗传算法或者禁忌搜索
    // int max_iter_count = 30;  // 单轮优化最大迭代次数，表示每次搜索的最大迭代次数
    // int population_size = 20;  // 种群大小，表示遗传算法中种群的大小
    // int tabu_list_length = 10;  // 禁忌表长度，表示禁忌搜索算法中禁忌表的长度
    // int max_repeat_count = 20;  // 最优解最大重复次数，达到最大重复次数则退出搜索

    // double search_mode_param = 0.5;            // 搜索模式参数0~1，配置选择随机或者贪心
    // double random_search_strategy_param = 0.5; // 搜索模式参数，配置选择遗传算法或者禁忌搜索
    // int max_repeat_count = 100;                 // 最优解最大重复次数，达到最大重复次数则退出搜索
    // int max_iter_count = 30;                   // 单轮优化最大迭代次数，表示每次搜索的最大迭代次数
    // int population_size = 20;                  // 种群大小，表示遗传算法中种群的大小
    // int tabu_list_length = 50;                 // 禁忌表长度，表示禁忌搜索算法中禁忌表的长度
    

    double search_mode_param = std::stod(argv[2]);            // 搜索模式参数0~1，配置选择随机或者贪心 0.5
    double random_search_strategy_param = std::stod(argv[3]); // 搜索模式参数，配置选择遗传算法或者禁忌搜索 0.5
    int max_repeat_count = std::stoi(argv[4]); // 最优解最大重复次数，达到最大重复次数则退出搜索   100
    int max_iter_count = std::stoi(argv[5]);   // 单轮优化最大迭代次数，表示每次搜索的最大迭代次数  30
    int population_size = std::stoi(argv[6]);  // 种群大小，表示遗传算法中种群的大小  20
    int tabu_list_length = std::stoi(argv[7]); // 禁忌表长度，表示禁忌搜索算法中禁忌表的长度 25
    const std::string algorithm_mode = argc > 8 ? argv[8] : "HLS";
    const std::vector<std::string> supported_modes = {
        "HLS", "GS", "GA", "TS", "GSGA", "GSTS", "GATS"
    };
    if (std::find(supported_modes.begin(), supported_modes.end(), algorithm_mode) == supported_modes.end()) {
        std::cerr << "Invalid algorithm_mode: " << algorithm_mode << std::endl;
        std::cerr << "Supported modes: HLS, GS, GA, TS, GSGA, GSTS, GATS" << std::endl;
        return 1;
    }

    // ==========================

    cout << "file_path: " << file_path << endl;
    cout << "search_mode_param: " << search_mode_param << endl;
    cout << "random_search_strategy_param: " << random_search_strategy_param << endl;
    cout << "max_repeat_count: " << max_repeat_count << endl;
    cout << "max_iter_count: " << max_iter_count << endl;
    cout << "population_size: " << population_size << endl;
    cout << "tabu_list_length: " << tabu_list_length << endl;
    cout << "algorithm_mode: " << algorithm_mode << endl;

    // if(argc > 2) {
    //     Schedule schedule0;
    //     if(strcmp(argv[2], "FIFO_EET") == 0) 
    //     {
    //         schedule0 = FIFO_EET(jobs, machines, jobList);
    //     } 
    //     else if(strcmp(argv[2], "FIFO_SPT") == 0) {
    //         schedule0 = FIFO_SPT(jobs, machines, jobList);
    //     }
    //     else if (strcmp(argv[2], "MOPNR_EET") == 0)
    //     {
    //         schedule0 = MOPNR_EET(jobs, machines, jobList);
    //     }
    //     else if (strcmp(argv[2], "MOPNR_SPT") == 0)
    //     {
    //         schedule0 = MOPNR_SPT(jobs, machines, jobList);
    //     }
    //     else if (strcmp(argv[2], "MWKR_EET") == 0)
    //     {
    //         schedule0 = MWKR_EET(jobs, machines, jobList);
    //     }
    //     else if (strcmp(argv[2], "MWKR_SPT") == 0)
    //     {
    //         schedule0 = MWKR_SPT(jobs, machines, jobList);
    //     }
    //     else
    //     {
    //         cout << "Invalid algorithm name. Please use FIFO, MOPNR, MWKR or SPT." << endl;
    //         return 1;
    //     }
    //     // schedule0.to_string();
    //     // schedule0.ToMapCode();
    //     const int ans0 = CalculateTotalTime(schedule0);
    //     cout << "Total time: " << ans0 << endl;

        
    //     // schedule0 = FIFO_EET(jobs, machines, jobList);
    //     // cout << "FIFO_EET Total time: " << CalculateTotalTime(schedule0) << endl;
    //     // schedule0 = FIFO_SPT(jobs, machines, jobList);
    //     // cout << "FIFO_SPT Total time: " << CalculateTotalTime(schedule0) << endl;
    //     // schedule0 = MOPNR_EET(jobs, machines, jobList);
    //     // cout << "MOPNR_EET Total time: " << CalculateTotalTime(schedule0) << endl;
    //     // schedule0 = MOPNR_SPT(jobs, machines, jobList);
    //     // cout << "MOPNR_SPT Total time: " << CalculateTotalTime(schedule0) << endl;
    //     // schedule0 = MWKR_EET(jobs, machines, jobList);
    //     // cout << "MWKR_EET Total time: " << CalculateTotalTime(schedule0) << endl;
    //     // schedule0 = MWKR_SPT(jobs, machines, jobList);
    //     // cout << "MWKR_SPT Total time: " << CalculateTotalTime(schedule0) << endl;
    //     // cout << endl;
    //     return 0;
    // }

    // 生成初始解
    Schedule schedule0 = GenerateInitialSolution(jobList, jobs, machines);
    // 获取file_path的文件名，并将目录修改为/data/luopw/production_scheduling/temp/
    string temp_file_path = "/data/luopw/production_scheduling/temp/" + file_path.substr(file_path.find_last_of("/") + 1);
    // std::cout << "temp_file_path: " << temp_file_path << std::endl;
    // Schedule schedule0 = GenerateSolution(temp_file_path, jobList, jobs, machines);

    // schedule0.to_string();
    // schedule0.ToMapCode();
    const int ans0 = CalculateTotalTime(schedule0);
    cout << "Total time: " << ans0 << endl;

    Schedule temp_schedule;
    input_schedule = schedule0;
    output_schedule = schedule0;

    int search_time = time(nullptr), count = 0, repeat_count =0;
    Schedule start_schedule = input_schedule;
    srand(time(nullptr));

    auto update_output_schedule = [&](const Schedule &candidate) {
        if (candidate.get_TotalTime() < output_schedule.get_TotalTime()) {
            output_schedule = candidate;
            input_schedule = candidate;
        }
    };

    auto run_greedy_search = [&]() {
        const int time1 = time(nullptr);
        Schedule candidate = GreedySearch(input_schedule, jobs, jobList, max_iter_count, machines);
        cout << "Greedy Search time: " << time(nullptr) - time1 << "s" << endl;
        return candidate;
    };

    auto run_genetic_search = [&]() {
        const int time1 = time(nullptr);
        Schedule candidate = AggressiveSearch(input_schedule, jobs, machines, jobList, population_size, max_iter_count);
        cout << "Genetic Algorithm time: " << time(nullptr) - time1 << "s" << endl;
        return candidate;
    };

    auto run_tabu_search = [&]() {
        const int time1 = time(nullptr);
        Schedule candidate = ConservativeSearch(input_schedule, jobs, jobList, tabu_list_length, max_iter_count, machines);
        cout << "Tabu Search time: " << time(nullptr) - time1 << "s" << endl;
        return candidate;
    };

    while (CheckStopFlag(search_time, count, repeat_count, max_repeat_count)) {
        double random_num = static_cast<double>(std::rand()) / RAND_MAX;

        if (algorithm_mode == "GS") {
            temp_schedule = run_greedy_search();
            update_output_schedule(temp_schedule);
        } else if (algorithm_mode == "GA") {
            temp_schedule = run_genetic_search();
            update_output_schedule(temp_schedule);
        } else if (algorithm_mode == "TS") {
            temp_schedule = run_tabu_search();
            update_output_schedule(temp_schedule);
        } else if (algorithm_mode == "GSGA") {
            // search_mode_param is the probability of selecting GA.
            if (random_num < search_mode_param) {
                temp_schedule = run_genetic_search();
            } else {
                temp_schedule = run_greedy_search();
            }
            update_output_schedule(temp_schedule);
        } else if (algorithm_mode == "GSTS") {
            // search_mode_param is the probability of selecting TS.
            if (random_num < search_mode_param) {
                temp_schedule = run_tabu_search();
            } else {
                temp_schedule = run_greedy_search();
            }
            update_output_schedule(temp_schedule);
        } else if (algorithm_mode == "GATS") {
            // random_search_strategy_param is the probability of selecting GA.
            if (random_num < random_search_strategy_param) {
                temp_schedule = run_genetic_search();
            } else {
                temp_schedule = run_tabu_search();
            }
            update_output_schedule(temp_schedule);
        } else {
            // Original HLS strategy: randomly choose greedy or random search;
            // a failed greedy round is followed by a random search round.
            if (random_num < search_mode_param) {
                temp_schedule = RandomSearch(input_schedule, jobs, machines, jobList, random_search_strategy_param, repeat_count, max_repeat_count, max_iter_count, tabu_list_length, population_size);
                update_output_schedule(temp_schedule);
            } else {
                temp_schedule = run_greedy_search();
                const int previous_best_time = output_schedule.get_TotalTime();
                update_output_schedule(temp_schedule);
                if (temp_schedule.get_TotalTime() >= previous_best_time) {
                    temp_schedule = RandomSearch(input_schedule, jobs, machines, jobList, random_search_strategy_param, repeat_count, max_repeat_count, max_iter_count, tabu_list_length, population_size);
                    update_output_schedule(temp_schedule);
                }
            }
        }

        if (output_schedule.get_TotalTime()<=start_schedule.get_TotalTime()) {
            if (output_schedule.get_TotalTime() < start_schedule.get_TotalTime()) {
                repeat_count = 1;
            } else {
                repeat_count++;
            }
            cout << "Total time: " << CalculateTotalTime(output_schedule) << endl;
        }
        count++;
        start_schedule = output_schedule;
    }

    // output_schedule.to_string();
    // output_schedule.ToMapCode();
    const int ans1 = CalculateTotalTime(output_schedule);
    // cout << "Total time: " << ans1 << endl;
    // cout << "Time cost: " << time(nullptr) - search_time << "s" << endl;
    // 对于file_path为fjsp.hurink.vdata-mt20.m5j20c5.txt 输出格式为：hurink.vdata-mt20.m5j20c5
    // cout << "search_time:" << time(nullptr) - search_time << "s" << endl;
    // cout << "repeat_count:" << repeat_count << endl;
    // cout << "count:" << count << endl;
    cout << ans1 << endl;
    cout << time(nullptr) - search_time << endl;
    cout << endl;
    return 0;
}
