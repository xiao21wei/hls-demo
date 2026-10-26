//
// Created by 28898 on 25-1-3.
//

#include "RandomSearch.h"

#include <algorithm>
#include <iostream>
#include <random>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "DataProc.h"
#include "TabuSearch.h"
#include <climits>

using namespace std;

namespace {

using JobNameMap = std::unordered_map<std::string, const Job *>;

JobNameMap BuildJobNameMap(const std::vector<Job> &jobs) {
    JobNameMap job_name_map;
    job_name_map.reserve(jobs.size());
    for (const auto &job : jobs) {
        job_name_map[job.get_job_name()] = &job;
    }
    return job_name_map;
}

std::vector<const Job *> BuildOrderedJobsByJobList(const std::vector<std::string> &jobList, const JobNameMap &job_name_map) {
    std::vector<const Job *> ordered_jobs;
    ordered_jobs.reserve(jobList.size());
    for (const auto &job_name : jobList) {
        const auto job_it = job_name_map.find(job_name);
        ordered_jobs.push_back(job_it == job_name_map.end() ? nullptr : job_it->second);
    }
    return ordered_jobs;
}

std::unordered_map<long long, int> BuildScheduleMachineMap(const Schedule &schedule) {
    std::unordered_map<long long, int> machine_map;
    const auto &schedule_items = schedule.get_schedule_items();
    for (int machine_index = 0; machine_index < schedule_items.size(); ++machine_index) {
        const auto &item = schedule_items[machine_index];
        for (const auto &process : item.schedule_process) {
            const long long key =
                (static_cast<long long>(process.job_id) << 32) ^
                static_cast<unsigned int>(process.process_id);
            machine_map[key] = item.machine_id;
        }
    }
    return machine_map;
}

std::string BuildChromosomeKey(const std::vector<int> &machine_selection_code, const std::vector<int> &operation_sequencing_code) {
    std::string key;
    key.reserve((machine_selection_code.size() + operation_sequencing_code.size()) * 4 + 1);
    for (const int value : machine_selection_code) {
        key += std::to_string(value);
        key.push_back(',');
    }
    key.push_back('|');
    for (const int value : operation_sequencing_code) {
        key += std::to_string(value);
        key.push_back(',');
    }
    return key;
}

std::mt19937 &GetSharedRng() {
    static thread_local std::mt19937 rng(std::random_device{}());
    return rng;
}

}  // namespace


Schedule RandomSearch(const Schedule &schedule, const std::vector<Job> &jobs, std::vector<Machine> &machines, const std::vector<std::string> &jobList, const double strategy_param, const int repeat_count, const int max_repeat_count, const int max_iter_count, const int tabu_list_length, const int population_size) {
    // ReSharper disable once CppTooWideScope
    std::vector<Schedule> scheduleList;
    Schedule best_schedule = schedule;
    const double random_num = static_cast<double>(std::rand()) / RAND_MAX;
    // if (random_num < strategy_param) {
    if (repeat_count<max_repeat_count/2 || random_num < strategy_param) {
        int time1 = time(nullptr);
        best_schedule = AggressiveSearch(schedule, jobs, machines, jobList, population_size, max_iter_count);
        cout << "Genetic Algorithm time: " <<  time(nullptr) - time1 << "s" << endl;
    } else {
        int time1 = time(nullptr);
        best_schedule = ConservativeSearch(best_schedule, jobs, jobList, tabu_list_length, max_iter_count, machines);
        cout << "Tabu Search time: " <<  time(nullptr)  - time1 << "s" << endl;
    }
    // int time1 = time(nullptr);
    // best_schedule = AggressiveSearch(schedule, jobs, machines, jobList, population_size, max_iter_count);
    // cout << "Genetic Algorithm time: " <<  time(nullptr) - time1 << "s" << endl;
    // best_schedule = ConservativeSearch(best_schedule, jobs, jobList, tabu_list_length, max_iter_count, machines);
    // cout << "Tabu Search time: " <<  time(nullptr)  - time1 << "s" << endl;
    return best_schedule;
}

Schedule ConservativeSearch(const Schedule &schedule, const std::vector<Job> &jobs, const std::vector<std::string> &jobList, int tabu_list_length, int max_iter_count, std::vector<Machine> &machines) {
    Schedule res_schedule = schedule;
    // TODO: ConservativeSearch, 保守搜索，使用禁忌搜索算法，对调度方案进行调整，返回调整后的调度方案列表
    res_schedule = TabuSearch(schedule, jobs, jobList, tabu_list_length, max_iter_count, machines);
    return res_schedule;
}

Schedule AggressiveSearch(const Schedule &schedule, const std::vector<Job> &jobs, std::vector<Machine> &machines, const std::vector<std::string> &jobList, int population_size, const int max_iter_count) {
    // TODO: AggressiveSearch, 激进搜索，使用遗传算法对调度方案进行调整，返回调整后的调度方案
    double cross_rate = 0.8;  // 交叉概率
    double variation_rate = 0.15;  // 变异概率
    double cross_machine_rate = 0.5;  // 机器选择编码交叉概率
    double variation_machine_rate = 0.5;  // 机器选择编码变异概率

    int optimal_fitness = INT_MAX;
    std::vector<int> optimal_machine_selection_code;
    std::vector<int> optimal_operation_sequencing_code;

    std::vector<std::vector<int>> population_machine_selection_code;  // 种群的机器选择编码
    std::vector<std::vector<int>> population_operation_sequencing_code;  // 种群的工序排序编码
    std::vector<std::vector<int>> temp_population_machine_selection_code;  // 临时种群的机器选择编码
    std::vector<std::vector<int>> temp_population_operation_sequencing_code;  // 临时种群的工序排序编码
    std::vector<int> temp_machine1, temp_machine2;
    std::vector<int> temp_operation1, temp_operation2;
    std::vector<int> fitness_list;
    std::unordered_map<std::string, int> fitness_cache;

    EncodeSchedule(schedule, jobs, jobList, optimal_machine_selection_code, optimal_operation_sequencing_code);
    optimal_fitness = schedule.get_TotalTime();
    fitness_cache.reserve(std::max(16, population_size * max_iter_count / 2));

    // 种群初始化
    GlobalInitializePopulation(jobs, machines, jobList, population_size*0.6, optimal_operation_sequencing_code, temp_population_machine_selection_code, temp_population_operation_sequencing_code);
    population_machine_selection_code.reserve(population_size);
    population_operation_sequencing_code.reserve(population_size);
    population_machine_selection_code.insert(population_machine_selection_code.end(), temp_population_machine_selection_code.begin(), temp_population_machine_selection_code.end());
    population_operation_sequencing_code.insert(population_operation_sequencing_code.end(), temp_population_operation_sequencing_code.begin(), temp_population_operation_sequencing_code.end());
    LocalInitializePopulation(jobs, machines, jobList, population_size*0.2, optimal_operation_sequencing_code, temp_population_machine_selection_code, temp_population_operation_sequencing_code);
    population_machine_selection_code.insert(population_machine_selection_code.end(), temp_population_machine_selection_code.begin(), temp_population_machine_selection_code.end());
    population_operation_sequencing_code.insert(population_operation_sequencing_code.end(), temp_population_operation_sequencing_code.begin(), temp_population_operation_sequencing_code.end());
    RandomInitializePopulation(jobs, machines, jobList, population_size*0.2, optimal_operation_sequencing_code, temp_population_machine_selection_code, temp_population_operation_sequencing_code);
    population_machine_selection_code.insert(population_machine_selection_code.end(), temp_population_machine_selection_code.begin(), temp_population_machine_selection_code.end());
    population_operation_sequencing_code.insert(population_operation_sequencing_code.end(), temp_population_operation_sequencing_code.begin(), temp_population_operation_sequencing_code.end());

    double random_num = 0;
    int population_count = population_machine_selection_code.size();
    std::vector<std::vector<int>> after_machine_selection_code;  // 种群的机器选择编码
    std::vector<std::vector<int>> after_operation_sequencing_code;  // 种群的工序排序编码
    std::vector<int> population_fitness(population_count, INT_MAX);
    for (int i = 0; i < population_count; ++i) {
        population_fitness[i] = CalculateFitness(
            population_machine_selection_code[i],
            population_operation_sequencing_code[i],
            jobs,
            jobList,
            schedule,
            machines,
            fitness_cache
        );
    }
    for (int k = 0; k < max_iter_count;k++) {
        for (int i =0 ;i<population_count;i++) {
            after_machine_selection_code.clear();
            after_operation_sequencing_code.clear();
            fitness_list.clear();
            after_machine_selection_code.reserve(5);
            after_operation_sequencing_code.reserve(5);
            fitness_list.reserve(5);
            random_num = static_cast<double>(std::rand()) / RAND_MAX;
            if (random_num < cross_rate) {
                int parent1 = std::rand() % population_machine_selection_code.size();
                random_num = static_cast<double>(std::rand()) / RAND_MAX;
                if (random_num < cross_machine_rate) {
                    MachineCross(population_machine_selection_code[i], population_machine_selection_code[parent1], temp_machine1, temp_machine2);
                    after_machine_selection_code.push_back(temp_machine1);
                    after_machine_selection_code.push_back(temp_machine2);
                    after_operation_sequencing_code.push_back(population_operation_sequencing_code[i]);
                    after_operation_sequencing_code.push_back(population_operation_sequencing_code[i]);
                    // std::cout << "MachineCross" << std::endl;
                } else {
                    OperationCross(population_operation_sequencing_code[i], population_operation_sequencing_code[parent1], temp_operation1, temp_operation2);
                    after_machine_selection_code.push_back(population_machine_selection_code[i]);
                    after_machine_selection_code.push_back(population_machine_selection_code[i]);
                    after_operation_sequencing_code.push_back(temp_operation1);
                    after_operation_sequencing_code.push_back(temp_operation2);
                    // std::cout << "OperationCross" << std::endl;
                }
                after_machine_selection_code.push_back(population_machine_selection_code[i]);
                after_operation_sequencing_code.push_back(population_operation_sequencing_code[i]);
            }
            random_num = static_cast<double>(std::rand()) / RAND_MAX;
            if (random_num < variation_rate) {
                random_num = static_cast<double>(std::rand()) / RAND_MAX;
                if (random_num < variation_machine_rate) {
                    MachineVariation(population_machine_selection_code[i], jobs, jobList, temp_machine1);
                    after_machine_selection_code.push_back(temp_machine1);
                    after_operation_sequencing_code.push_back(population_operation_sequencing_code[i]);
                    // std::cout << "MachineVariation" << std::endl;
                } else {
                    OperationVariation(population_operation_sequencing_code[i], temp_operation1);
                    after_machine_selection_code.push_back(population_machine_selection_code[i]);
                    after_operation_sequencing_code.push_back(temp_operation1);
                    // std::cout << "OperationVariation" << std::endl;
                }
                after_machine_selection_code.push_back(population_machine_selection_code[i]);
                after_operation_sequencing_code.push_back(population_operation_sequencing_code[i]);
            }
            if (after_machine_selection_code.size()!=0) {
                for (int j =0;j<after_machine_selection_code.size();j++) {
                    fitness_list.push_back(CalculateFitness(
                        after_machine_selection_code[j],
                        after_operation_sequencing_code[j],
                        jobs,
                        jobList,
                        schedule,
                        machines,
                        fitness_cache
                    ));
                }
                int min_fitness_index = 0;
                int min_fitness = fitness_list[0];
                for (int j = 1; j<fitness_list.size();j++) {
                    if (fitness_list[j] < min_fitness) {
                        min_fitness = fitness_list[j];
                        min_fitness_index = j;
                    }
                }
                population_machine_selection_code[i] = after_machine_selection_code[min_fitness_index];
                population_operation_sequencing_code[i] = after_operation_sequencing_code[min_fitness_index];
                population_fitness[i] = min_fitness;
            }
        }
        // std::cout << "finished " << k << "th iteration" << std::endl;
    }

    for (int i =0 ;i<population_machine_selection_code.size();i++) {
        if (population_fitness[i] < optimal_fitness) {
            optimal_fitness = population_fitness[i];
            optimal_machine_selection_code = population_machine_selection_code[i];
            optimal_operation_sequencing_code = population_operation_sequencing_code[i];
        }
    }
    return DecodeSchedule(optimal_machine_selection_code, optimal_operation_sequencing_code, jobs, jobList, schedule, machines);
}

// 将调度方案编码为机器选择编码和工序排序编码
void EncodeSchedule(const Schedule &schedule, const std::vector<Job> &jobs, const std::vector<std::string> &jobList, std::vector<int> &machine_selection_code, std::vector<int> &operation_sequencing_code) {
    machine_selection_code.clear();
    operation_sequencing_code.clear();
    machine_selection_code.reserve(schedule.get_processList().size());
    operation_sequencing_code.reserve(schedule.get_processList().size());
    const auto job_name_map = BuildJobNameMap(jobs);
    const auto ordered_jobs = BuildOrderedJobsByJobList(jobList, job_name_map);
    const auto schedule_machine_map = BuildScheduleMachineMap(schedule);
    for (const Job *job_ptr : ordered_jobs) {
        if (job_ptr == nullptr) {
            continue;
        }
        const Job &job = *job_ptr;
        // 根据工件的工序信息，生成编码
        for (const auto &job_process: job.get_job_process()) {
            //获取当前工序所在的机器的编号
            const long long key =
                (static_cast<long long>(job.get_job_id()) << 32) ^
                static_cast<unsigned int>(job_process.process_item[0].process_id);
            const auto machine_it = schedule_machine_map.find(key);
            const int temp_machine_id = machine_it == schedule_machine_map.end() ? 0 : machine_it->second;
            for (int i =0;i<job_process.machine_count;i++) {
                if (job_process.process_item[i].machine_id == temp_machine_id) {
                    machine_selection_code.push_back(i);
                    break;
                }
            }
        }
    }
    for (int i = 1;i<schedule.get_processList().size()-1;i++) {

        operation_sequencing_code.push_back(std::stoi(schedule.get_processList()[i].substr(0, schedule.get_processList()[i].find('-'))));
    }
}

// 将机器选择编码和工序排序编码解码为调度方案
Schedule DecodeSchedule(const std::vector<int> &machine_selection_code, const std::vector<int> &operation_sequencing_code, const std::vector<Job> &jobs, const std::vector<std::string> &jobList, const Schedule &schedule, std::vector<Machine> &machines) {
    std::vector<std::vector<int>> machine_matrix;
    machine_matrix.reserve(jobs.size());
    std::vector<int> now_process_id(jobs.size(), 0);
    for (int i = 0; i<jobs.size();i++) {
        machine_matrix.push_back(std::vector<int>(jobs[i].get_process_count(), -1));
    }
    const auto job_name_map = BuildJobNameMap(jobs);
    const auto ordered_jobs = BuildOrderedJobsByJobList(jobList, job_name_map);
    size_t machine_code_index = 0;
    for (const Job *job_ptr : ordered_jobs) {
        if (job_ptr == nullptr) {
            continue;
        }
        const Job &job = *job_ptr;
        for (const auto &job_process: job.get_job_process()) {
            const int temp_process_id = job_process.process_item[0].process_id;
            const int machine_selected = machine_selection_code[machine_code_index++];
            machine_matrix[job.get_job_id()][temp_process_id] = job_process.process_item[machine_selected].machine_id;
        }
    }
    // for (int i =0;i<machine_selection_code.size(); i++) {
    //     std::cout << machine_selection_code[i] << " ";
    // }
    // std::cout << std::endl;
    // for (int i =0;i<operation_sequencing_code.size(); i++) {
    //     std::cout << operation_sequencing_code[i] << " ";
    // }
    // std::cout << std::endl;
    // for (int i =0;i< machine_matrix.size();i++) {
    //     for (int j =0;j<machine_matrix[i].size();j++) {
    //         if (machine_matrix[i][j] != -1) {
    //             std::cout << machine_matrix[i][j] <<" ";
    //         }
    //     }
    //     std::cout << std::endl;
    // }
    Schedule res_schedule;
    res_schedule.set_schedule_id(schedule.get_schedule_id()+1);
    res_schedule.set_machine_count(schedule.get_machine_count());
    std::vector<Schedule_item> temp_schedule_items;
    temp_schedule_items.reserve(schedule.get_machine_count());
    for (int i = 0; i<schedule.get_machine_count(); i++) {
        Schedule_item temp_schedule_item;
        temp_schedule_item.machine_id = i;
        temp_schedule_item.process_count = 0;
        temp_schedule_item.schedule_process.clear();
        temp_schedule_items.push_back(temp_schedule_item);
    }
    for (int i = 0; i<operation_sequencing_code.size(); i++) {
        int temp_job_id = operation_sequencing_code[i];
        int temp_process_id = now_process_id[temp_job_id];
        int temp_machine_id = machine_matrix[temp_job_id][temp_process_id];
        temp_schedule_items[temp_machine_id].schedule_process.push_back({temp_job_id, temp_process_id});
        temp_schedule_items[temp_machine_id].process_count++;
        now_process_id[temp_job_id]++;
    }
    res_schedule.set_schedule_items(temp_schedule_items);
    std::vector<std::vector<int>> graph = ScheduleItemsToGraph(res_schedule, temp_schedule_items, jobs, jobList, true, machines);
    return res_schedule;
}

// 根据工序获取机器编号
int GetMachineIdByJobIdAndProcessId(const Schedule &schedule, const int job_id, const int process_id) {
    for (int i = 0; i < schedule.get_machine_count(); i++) {
        for (int j = 0; j < schedule.get_schedule_items()[i].process_count; j++) {
            if (schedule.get_schedule_items()[i].schedule_process[j].job_id == job_id &&
                schedule.get_schedule_items()[i].schedule_process[j].process_id == process_id) {
                return i;
            }
        }
    }
    return 0;
}

// 全局初始化种群
void GlobalInitializePopulation(const std::vector<Job> &jobs, const std::vector<Machine> &machines,const std::vector<std::string> &jobList, const int population_size, const std::vector<int> &operation_sequencing_code,
    std::vector<std::vector<int>> &population_machine_selection_code, std::vector<std::vector<int>> &population_operation_sequencing_code) {

    population_machine_selection_code.clear();
    population_operation_sequencing_code.clear();
    population_machine_selection_code.reserve(population_size);
    population_operation_sequencing_code.reserve(population_size);
    const auto job_name_map = BuildJobNameMap(jobs);
    const auto ordered_jobs = BuildOrderedJobsByJobList(jobList, job_name_map);
    std::unordered_map<std::string, std::vector<int>> temp_machine_selection_code_map;
    temp_machine_selection_code_map.reserve(jobList.size());
    auto &rng = GetSharedRng();

    for (int k=0;k<population_size;k++) {
        temp_machine_selection_code_map.clear();
        std::vector<int> temp_machine_selection_code;
        temp_machine_selection_code.reserve(operation_sequencing_code.size());
        auto machine_time = std::vector<int>(machines.size(), 0);
        std::vector<int> remaining_indices(ordered_jobs.size());
        for (int i = 0; i < remaining_indices.size(); ++i) {
            remaining_indices[i] = i;
        }
        while (!remaining_indices.empty()) {
            const int picked_position = std::rand() % remaining_indices.size();
            const int picked_index = remaining_indices[picked_position];
            const Job *job_ptr = ordered_jobs[picked_index];
            if (job_ptr == nullptr) {
                remaining_indices[picked_position] = remaining_indices.back();
                remaining_indices.pop_back();
                continue;
            }
            const Job &temp_job = *job_ptr;
            const std::string &picked_job_name = jobList[picked_index];
            for (const Job_process &job_process: temp_job.get_job_process()) {
                auto operation_time = std::vector<int>(job_process.machine_count, 0);
                for (int i = 0; i < job_process.machine_count; i++) {
                    operation_time[i] = job_process.process_item[i].process_time + machine_time[job_process.process_item[i].machine_id];
                }
                int min_time = operation_time[0];
                int min_machine = 0;
                for (int i = 1; i < operation_time.size(); i++) {
                    if (operation_time[i] < min_time) {
                        min_time = operation_time[i];
                        min_machine = i;
                    }
                }
                machine_time[job_process.process_item[min_machine].machine_id] = min_time;
                if (temp_machine_selection_code_map.find(picked_job_name) == temp_machine_selection_code_map.end()) {
                    temp_machine_selection_code_map[picked_job_name] = std::vector<int>();
                }
                temp_machine_selection_code_map[picked_job_name].push_back(min_machine);
            }
            remaining_indices[picked_position] = remaining_indices.back();
            remaining_indices.pop_back();
        }
        std::vector<int> temp_operation_sequencing_code = operation_sequencing_code;
        std::shuffle(temp_operation_sequencing_code.begin(), temp_operation_sequencing_code.end(), rng);

        for (int i =0; i<jobList.size();i++) {
            const std::string &temp_job_name = jobList[i];
            const auto map_it = temp_machine_selection_code_map.find(temp_job_name);
            if (map_it != temp_machine_selection_code_map.end()) {
                for (int j =0;j<map_it->second.size();j++) {
                    temp_machine_selection_code.push_back(map_it->second[j]);
                }
            }
        }
        population_machine_selection_code.push_back(temp_machine_selection_code);
        population_operation_sequencing_code.push_back(temp_operation_sequencing_code);
    }
}

// 本地初始化种群
void LocalInitializePopulation(const std::vector<Job> &jobs, const std::vector<Machine> &machines, const std::vector<std::string> &jobList, const int population_size, const std::vector<int> &operation_sequencing_code,
    std::vector<std::vector<int>> &population_machine_selection_code, std::vector<std::vector<int>> &population_operation_sequencing_code) {
    population_machine_selection_code.clear();
    population_operation_sequencing_code.clear();
    population_machine_selection_code.reserve(population_size);
    population_operation_sequencing_code.reserve(population_size);
    const auto job_name_map = BuildJobNameMap(jobs);
    const auto ordered_jobs = BuildOrderedJobsByJobList(jobList, job_name_map);
    auto &rng = GetSharedRng();

    for (int k = 0; k < population_size; k++) {
        std::vector<int> temp_machine_selection_code;
        temp_machine_selection_code.reserve(operation_sequencing_code.size());
        auto machine_time = std::vector<int>(machines.size(), 0);
        for (const Job *job_ptr : ordered_jobs) {
            if (job_ptr == nullptr) {
                continue;
            }
            const Job &temp_job = *job_ptr;
            for (const Job_process &job_process: temp_job.get_job_process()) {
                auto operation_time = std::vector<int>(job_process.machine_count, 0);
                for (int i = 0; i < job_process.machine_count; i++) {
                    operation_time[i] = job_process.process_item[i].process_time + machine_time[job_process.process_item[i].machine_id];
                }
                int min_time = operation_time[0];
                int min_machine = 0;
                for (int i = 1; i < operation_time.size(); i++) {
                    if (operation_time[i] < min_time) {
                        min_time = operation_time[i];
                        min_machine = i;
                    }
                }
                machine_time[job_process.process_item[min_machine].machine_id] = min_time;
                temp_machine_selection_code.push_back(min_machine);
            }
            machine_time = std::vector<int>(machines.size(), 0);  // 重置机器时间
        }
        std::vector<int> temp_operation_sequencing_code = operation_sequencing_code;
        std::shuffle(temp_operation_sequencing_code.begin(), temp_operation_sequencing_code.end(), rng);

        population_machine_selection_code.push_back(temp_machine_selection_code);
        population_operation_sequencing_code.push_back(temp_operation_sequencing_code);
    }
}

// 随机初始化种群
void RandomInitializePopulation(const std::vector<Job> &jobs, const std::vector<Machine> &machines, const std::vector<std::string> &jobList, const int population_size, const std::vector<int> &operation_sequencing_code,
    std::vector<std::vector<int>> &population_machine_selection_code, std::vector<std::vector<int>> &population_operation_sequencing_code) {
    population_machine_selection_code.clear();
    population_operation_sequencing_code.clear();
    population_machine_selection_code.reserve(population_size);
    population_operation_sequencing_code.reserve(population_size);
    const auto job_name_map = BuildJobNameMap(jobs);
    const auto ordered_jobs = BuildOrderedJobsByJobList(jobList, job_name_map);
    auto &rng = GetSharedRng();

    for (int k = 0; k < population_size; k++) {
        std::vector<int> temp_machine_selection_code;
        temp_machine_selection_code.reserve(operation_sequencing_code.size());
        for (const Job *job_ptr : ordered_jobs) {
            if (job_ptr == nullptr) {
                continue;
            }
            const Job &job = *job_ptr;
            for (const auto &job_process: job.get_job_process()) {
                const int temp_machine_id = std::rand() % job_process.machine_count;
                temp_machine_selection_code.push_back(temp_machine_id);
            }
        }
        std::vector<int> temp_operation_sequencing_code = operation_sequencing_code;
        std::shuffle(temp_operation_sequencing_code.begin(), temp_operation_sequencing_code.end(), rng);

        population_machine_selection_code.push_back(temp_machine_selection_code);
        population_operation_sequencing_code.push_back(temp_operation_sequencing_code);
    }
}

// 机器选择编码交叉
void MachineCross(const std::vector<int> &parent1, const std::vector<int> &parent2, std::vector<int> &child1, std::vector<int> &child2) {

    child1.clear();
    child2.clear();
    const int parent_count = parent1.size();
    const int r = std::rand() % parent_count;
    child1.reserve(parent_count);
    child2.reserve(parent_count);
    std::vector<char> selected(parent_count, 0);
    for (int i = 0; i < r; i++) {
        int temp = std::rand() % parent_count;
        while (selected[temp]) {
            temp = std::rand() % parent_count;
        }
        selected[temp] = 1;
    }
    for (int i = 0; i < parent_count; i++) {
        if (selected[i]) {
            child1.push_back(parent1[i]);
            child2.push_back(parent2[i]);
        }
        else {
            child1.push_back(parent2[i]);
            child2.push_back(parent1[i]);
        }
    }
}

void OperationCross(const std::vector<int> &parent1, const std::vector<int> &parent2, std::vector<int> &child1, std::vector<int> &child2) {
    const int parent_count = parent1.size();
    child1.clear();
    child2.clear();
    child1.reserve(parent_count);
    child2.reserve(parent_count);
    std::vector<int> job_id_list;
    job_id_list.reserve(parent_count);
    std::unordered_set<int> seen_jobs;
    seen_jobs.reserve(parent_count);
    for (int i = 0; i < parent_count; i++) {
        if (seen_jobs.insert(parent1[i]).second) {
            job_id_list.push_back(parent1[i]);
        }
    }
    std::unordered_set<int> job_set1;
    job_set1.reserve(job_id_list.size());
    int random_num = 0;
    for (int i = 0; i < job_id_list.size(); i++) {
        random_num = std::rand() % 2;
        if (random_num == 0) {
            job_set1.insert(job_id_list[i]);
        }
    }
    int index =0;
    for (int i =0; i< parent1.size();i++) {
        if (job_set1.find(parent1[i]) != job_set1.end()) {
            child1.push_back(parent1[i]);
        }
        else {
            for (int k=index; k<parent2.size(); k++) {
                if (job_set1.find(parent2[k]) == job_set1.end()) {
                    child1.push_back(parent2[k]);
                    index = k+1;
                    break;
                }
            }
        }
    }
    index = 0;
    for (int i = 0; i < parent2.size(); i++) {
        if (job_set1.find(parent2[i]) == job_set1.end()) {
            child2.push_back(parent2[i]);
        }
        else {
            for (int k = index; k < parent1.size(); k++) {
                if (job_set1.find(parent1[k]) != job_set1.end()) {
                    child2.push_back(parent1[k]);
                    index = k + 1;
                    break;
                }
            }
        }
    }
}

void MachineVariation(const std::vector<int> &parent, const std::vector<Job> &jobs, const std::vector<std::string> &jobList, std::vector<int> &child) {
    child = parent;
    const int parent_count = parent.size();
    const int r = std::rand() % parent_count;
    std::vector<char> selected(parent_count, 0);  // 变异点标记
    for (int i = 0; i < r; i++) {
        int temp = std::rand() % parent_count;
        while (selected[temp]) {
            temp = std::rand() % parent_count;
        }
        selected[temp] = 1;
    }
    int now_index = -1;
    const auto job_name_map = BuildJobNameMap(jobs);
    const auto ordered_jobs = BuildOrderedJobsByJobList(jobList, job_name_map);
    for (const Job *job_ptr : ordered_jobs) {
        if (job_ptr == nullptr) {
            continue;
        }
        const Job &job = *job_ptr;
        for (const auto &job_process: job.get_job_process()) {
            now_index++;
            if (selected[now_index]) {
                int min_machine = 0;
                int min_time = job_process.process_item[0].process_time;
                for (int i = 1; i < job_process.machine_count; i++) {
                    if (job_process.process_item[i].process_time < min_time) {
                        min_time = job_process.process_item[i].process_time;
                        min_machine = i;
                    }
                }
                child[now_index] = min_machine;
            }
        }
    }
}

void OperationVariation(const std::vector<int> &parent, std::vector<int> &child) {
    child = parent;
    auto &rng = GetSharedRng();
    std::shuffle(child.begin(), child.end(), rng);
}

int CalculateFitness(const std::vector<int> &machine_selection_code, const std::vector<int> &operation_sequencing_code, const std::vector<Job> &jobs, const std::vector<std::string> &jobList, const Schedule &schedule0, std::vector<Machine> &machines, std::unordered_map<std::string, int> &fitness_cache) {
    const std::string cache_key = BuildChromosomeKey(machine_selection_code, operation_sequencing_code);
    const auto cache_it = fitness_cache.find(cache_key);
    if (cache_it != fitness_cache.end()) {
        return cache_it->second;
    }
    Schedule schedule = DecodeSchedule(machine_selection_code, operation_sequencing_code, jobs, jobList, schedule0, machines);
    const int fitness = CalculateTotalTime(schedule);
    fitness_cache.emplace(cache_key, fitness);
    return fitness;
}

