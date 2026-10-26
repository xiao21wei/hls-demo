//
// Created by luopw on 24-12-1.
//

#include "GreedySearch.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <ostream>
#include <queue>
#include <unordered_map>

#include "DataProc.h"
#include <climits>

namespace {

std::string BuildProcessKey(const int job_id, const int process_id, const bool with_job_prefix) {
    if (with_job_prefix) {
        return "job" + std::to_string(job_id) + "-" + std::to_string(process_id);
    }
    return std::to_string(job_id) + "-" + std::to_string(process_id);
}

struct ProcessLocation {
    int machine_index;
    int item_index;
};

std::unordered_map<std::string, ProcessLocation> BuildProcessLocationMap(
    const std::vector<Schedule_item> &schedule_items,
    const bool with_job_prefix
) {
    std::unordered_map<std::string, ProcessLocation> location_map;
    for (int machine_index = 0; machine_index < schedule_items.size(); ++machine_index) {
        const auto &schedule_processes = schedule_items[machine_index].schedule_process;
        for (int item_index = 0; item_index < schedule_processes.size(); ++item_index) {
            const auto &process = schedule_processes[item_index];
            location_map[BuildProcessKey(process.job_id, process.process_id, with_job_prefix)] = {
                machine_index,
                item_index
            };
        }
    }
    return location_map;
}

std::unordered_map<int, int> BuildMachineProcessCountMap(const std::vector<Schedule_item> &schedule_items) {
    std::unordered_map<int, int> process_count_map;
    for (const auto &item : schedule_items) {
        process_count_map[item.machine_id] = item.process_count;
    }
    return process_count_map;
}

std::unordered_map<int, const Job *> BuildJobIdMap(const std::vector<Job> &jobs) {
    std::unordered_map<int, const Job *> job_id_map;
    job_id_map.reserve(jobs.size());
    for (const auto &job : jobs) {
        job_id_map[job.get_job_id()] = &job;
    }
    return job_id_map;
}

std::unordered_map<std::string, const Job *> BuildJobNameMap(const std::vector<Job> &jobs) {
    std::unordered_map<std::string, const Job *> job_name_map;
    job_name_map.reserve(jobs.size());
    for (const auto &job : jobs) {
        job_name_map[job.get_job_name()] = &job;
    }
    return job_name_map;
}

std::vector<std::string> BuildProcessListFromScheduleItems(const std::vector<Schedule_item> &schedule_items) {
    std::vector<std::string> process_list;
    process_list.reserve(2);
    process_list.push_back("start");
    for (const auto &item : schedule_items) {
        for (const auto &process : item.schedule_process) {
            process_list.push_back(BuildProcessKey(process.job_id, process.process_id, false));
        }
    }
    process_list.push_back("end");
    return process_list;
}

void FinalizeAcceptedSchedule(
    Schedule &schedule,
    const Schedule &base_schedule,
    const std::vector<Schedule_item> &schedule_items,
    const std::vector<std::vector<int>> &graph,
    const std::vector<Machine> &machines
) {
    schedule.set_schedule_id(base_schedule.get_schedule_id() + 1);
    schedule.set_machine_count(base_schedule.get_machine_count());
    schedule.set_schedule_items(schedule_items);
    schedule.set_graph(graph);
    schedule.set_processList(BuildProcessListFromScheduleItems(schedule_items));
    CalculateTotalFailureRate(schedule, machines);
}

}  // namespace


Schedule GreedySearch(const Schedule &schedule, const std::vector<Job> &jobs, const std::vector<std::string> &jobList, const int iter_count, std::vector<Machine> &machines) {
    std::vector<Schedule> scheduleList;
    Schedule temp_schedule = schedule;
    std::vector<std::string> processlist1 = ScoreProcessForExchange(schedule, jobs, jobList);
    std::vector<std::string> processlist2 = ScoreProcessForMove(schedule, jobs, jobList);
    bool flag1 = false, flag2 = false;
    while (scheduleList.size() < iter_count && (processlist1.size()>0 || processlist2.size()> 0)) {
        flag1 = false, flag2 = false;
        if (processlist1.size() > 0) {
            temp_schedule = ExchangeNeighborSearch(schedule, jobs, jobList, processlist1, flag1, machines);
            if (temp_schedule.get_schedule_id()!=-1) scheduleList.push_back(temp_schedule);
        }
        if (processlist2.size() > 0) {
            temp_schedule = MoveNeighborSearch(schedule, jobs, jobList, processlist2, flag2, machines);
            if (temp_schedule.get_schedule_id()!=-1) scheduleList.push_back(temp_schedule);
        }
        if (!flag1 && !flag2) break;
    }
    if (scheduleList.size() == 0) {
        return schedule;
    }
    Schedule best_schedule = scheduleList[0];
    for (Schedule temp_schedule1: scheduleList) {
        if (temp_schedule1.get_TotalTime() < best_schedule.get_TotalTime()) {
            best_schedule = temp_schedule1;
        }
    }
    return best_schedule;
}

// 已弃用
Schedule GreedyAdjust(const Schedule &schedule, const std::vector<Job> &jobs, const std::vector<std::string> &jobList, std::vector<Machine> &machines) {
    Schedule schedule1;

    const std::vector<std::vector<int>> graph = schedule.get_graph();
    std::vector<std::string> processList = schedule.get_processList();
    std::vector<Schedule_item> schedule_items = schedule.get_schedule_items();
    std::vector<std::string> key_process = schedule.GetKeyProcess();

    std::cout << std::endl;
    for (const auto &process: key_process) {
        std::cout << process << " ";
    }
    std::cout << std::endl;
    std::cout << std::endl;

    // 遍历每个机器上的工序，如果该工序为关键工序，则将其移动到前一个工序，重新计算此时的时间开销，如果时间开销减小，则保留，否则还原
    bool flag = false;
    for (int i = 0; i<schedule_items.size(); i++) {
        for (int j = 1; j<schedule_items[i].process_count;j++) {
            int job_id = schedule_items[i].schedule_process[j].job_id;
            int process_id = schedule_items[i].schedule_process[j].process_id;
            std::string temp_process = std::to_string(job_id) + "-" + std::to_string(process_id);
            // std::cout << "now process: " << temp_process << std::endl;
            // for (int k = 0;k<key_process.size(); k++) {
                // if (temp_process == key_process[k]) {
                    // std::cout << schedule_items[i].schedule_process[j-1].job_id << "-" << schedule_items[i].schedule_process[j-1].process_id << std::endl;
                    schedule_items[i].schedule_process[j].job_id = schedule_items[i].schedule_process[j-1].job_id;
                    schedule_items[i].schedule_process[j].process_id = schedule_items[i].schedule_process[j-1].process_id;
                    schedule_items[i].schedule_process[j-1].job_id = job_id;
                    schedule_items[i].schedule_process[j-1].process_id = process_id;
                    std::vector<std::vector<int>> graph1 = ScheduleItemsToGraph(schedule1, schedule_items, jobs, jobList, false, machines);
                    schedule1.set_graph(graph1);
                    // std::cout << hasCycle(graph1) << std::endl;
                    if (!hasCycle(graph1)) {
                        std::cout << "old time: " << schedule.get_TotalTime() << ", new time: " << CalculateTotalTime(schedule1) << std::endl;
                    }
                    if (!hasCycle(graph1) && CalculateTotalTime(schedule1) < schedule.get_TotalTime()) {
                        std::cout << "old time: " << schedule.get_TotalTime() << ", new time: " << CalculateTotalTime(schedule1) << std::endl;
                        flag = true;
                        break;
                    } else {
                        schedule_items[i].schedule_process[j-1].job_id = schedule_items[i].schedule_process[j].job_id;
                        schedule_items[i].schedule_process[j-1].process_id = schedule_items[i].schedule_process[j].process_id;
                        schedule_items[i].schedule_process[j].job_id = job_id;
                        schedule_items[i].schedule_process[j].process_id = process_id;
                    }
                // }
            // }
            // if (flag) break;
        }
        if (flag) break;
    }
    schedule1.set_schedule_id(schedule.get_schedule_id()+1);
    schedule1.set_machine_count(schedule.get_machine_count());
    schedule1.set_schedule_items(schedule_items);
    std::vector<std::vector<int>> graph1 = ScheduleItemsToGraph(schedule1, schedule_items, jobs, jobList, true, machines);
    return schedule1;
}

bool hasCycle(const std::vector<std::vector<int>> &adjMatrix) {  // 判断该邻接链表是否有环，如果有环返回true，否则返回false
    const int n = adjMatrix.size();
    std::vector<int> inDegree(n, 0);

    // 计算每个节点的入度
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            if (adjMatrix[i][j] != -1) { // 存在一条从 i 到 j 的边
                inDegree[j]++;
            }
        }
    }

    // 将所有入度为 0 的节点加入队列
    std::queue<int> q;
    for (int i = 0; i < n; ++i) {
        if (inDegree[i] == 0) {
            q.push(i);
        }
    }

    int visitedCount = 0;

    // 拓扑排序
    while (!q.empty()) {
        const int node = q.front();
        q.pop();
        visitedCount++;

        for (int j = 0; j < n; ++j) {
            if (adjMatrix[node][j] != -1) { // 存在一条从 node 到 j 的边
                inDegree[j]--;
                if (inDegree[j] == 0) {
                    q.push(j);
                }
            }
        }
    }
    // 如果未能访问所有节点，说明存在环
    return visitedCount != n;
}


Schedule ExchangeNeighborSearch(const Schedule &schedule, const std::vector<Job> &jobs, const std::vector<std::string> &jobList, std::vector<std::string> &processlist, bool &flag, std::vector<Machine> &machines) {
    Schedule res_schedule = schedule;
    res_schedule.set_schedule_id(-1);

    const std::vector<Schedule_item> original_schedule_items = schedule.get_schedule_items();
    std::vector<Schedule_item> schedule_items = original_schedule_items;
    const auto process_location_map = BuildProcessLocationMap(original_schedule_items, true);
    const int original_total_time = schedule.get_TotalTime();
    // TODO: 贪心邻域搜索，基于相同机器上相邻的工序进行最大程度的交换，返回调整后的调度方案
    // for (int i =0;i< processlist.size();i++) {
    //     std::cout << processlist[i] << " ";
    // }
    // std::cout << std::endl;
    int machine_id = -1, item_id = -1, job_id = 0, process_id = 0;
    std::string temp_process = "";
    for (const auto &process: processlist) {
        const auto location_it = process_location_map.find(process);
        if (location_it == process_location_map.end()) {
            continue;
        }
        machine_id = location_it->second.machine_index;
        item_id = location_it->second.item_index;
        if (item_id>0) {
            job_id = schedule_items[machine_id].schedule_process[item_id].job_id;
            process_id = schedule_items[machine_id].schedule_process[item_id].process_id;
            schedule_items[machine_id].schedule_process[item_id].job_id = schedule_items[machine_id].schedule_process[item_id-1].job_id;
            schedule_items[machine_id].schedule_process[item_id].process_id = schedule_items[machine_id].schedule_process[item_id-1].process_id;
            schedule_items[machine_id].schedule_process[item_id-1].job_id = job_id;
            schedule_items[machine_id].schedule_process[item_id-1].process_id = process_id;
            std::vector<std::vector<int>> graph1 = ScheduleItemsToGraph(res_schedule, schedule_items, jobs, jobList, false, machines);
            // std::cout << job_id << "-" << process_id << std::endl;
            // std::cout << "now graph status: " << hasCycle(graph1) << std::endl;
            // if (!hasCycle(graph1)) std::cout << "old time: " << schedule.get_TotalTime() << ", new time: " << CalculateTotalTime(res_schedule) << std::endl;
            if (!hasCycle(graph1)) {
                res_schedule.set_graph(graph1);
                if (CalculateTotalTime(res_schedule) < original_total_time) {
                    FinalizeAcceptedSchedule(res_schedule, schedule, schedule_items, graph1, machines);
                // std::cout <<"\n====exchange " << job_id << "-" << process_id<<" with previous process" << std::endl;
                    temp_process = process;
                    flag = true;
                    break;
                }
            }
            schedule_items = original_schedule_items;
        }
    }
    if (temp_process != "")
        processlist.erase(std::remove(processlist.begin(), processlist.end(), temp_process), processlist.end());
    return res_schedule;
}

// 交换评分函数，根据当前调度方案中工序编号和位置编号的关系，计算优先级，返回工序列表
std::vector<std::string> ScoreProcessForExchange(const Schedule &schedule, const std::vector<Job> &jobs, const std::vector<std::string> &jobList) {
    std::vector<std::string> res_processes;
    const auto schedule_items = schedule.get_schedule_items();
    std::unordered_map<std::string, int> process_index_map;
    process_index_map.reserve(schedule_items.size() * 4);
    for (const auto &item : schedule_items) {
        for (int index = 0; index < item.process_count; ++index) {
            const auto &process = item.schedule_process[index];
            process_index_map[BuildProcessKey(process.job_id, process.process_id, false)] = index;
        }
    }
    const auto job_name_map = BuildJobNameMap(jobs);
    // 工序编号越小，但调度位置较为靠后的工序，影响程度越大
    // 优先选择工序编号小的工序，工序编号相同时，选择调度位置较为靠后的工序，返回工序名称，使用algorithm库中的sort函数进行排序
    std::vector<std::pair<std::string, int>> temp_vec;
    temp_vec.reserve(jobList.size() * 4);
    for (const auto &job: jobList) {
        const auto job_it = job_name_map.find(job);
        if (job_it == job_name_map.end()) {
            continue;
        }
        const Job &job_item = *job_it->second;
        for (int j = 0; j < job_item.get_process_count(); ++j) {
            const std::string process_name = job_item.get_job_name() + "-" + std::to_string(j);
            const auto index_it = process_index_map.find(process_name);
            if (index_it != process_index_map.end()) {
                temp_vec.emplace_back(process_name, index_it->second);
            }
        }
    }
    // 按照工序编号从小到大排序，工序编号相同时，按照调度位置从大到小排序，工序编号从key中截取-后的数字进行比较
    std::sort(temp_vec.begin(), temp_vec.end(), [](const std::pair<std::string, int> &a, const std::pair<std::string, int> &b) {
        return std::stoi(a.first.substr(a.first.find('-')+1)) < std::stoi(b.first.substr(b.first.find('-')+1)) ||
               (std::stoi(a.first.substr(a.first.find('-')+1)) == std::stoi(b.first.substr(b.first.find('-')+1)) && a.second > b.second);
    });

    // 将排序后的工序名称存入res_processes中
    res_processes.reserve(temp_vec.size());
    for (const auto &item : temp_vec) {
        res_processes.push_back(item.first);
    }
    return res_processes;
}

// 获取调度位置编号
int GetItemIndex(const Schedule &schedule, const int job_id, const int process_id) {
    for (int i =0 ; i<schedule.get_schedule_items().size(); i++) {
        for (int j = 0; j<schedule.get_schedule_items()[i].process_count; j++) {
            if (schedule.get_schedule_items()[i].schedule_process[j].job_id == job_id && schedule.get_schedule_items()[i].schedule_process[j].process_id == process_id) {
                return j;
            }
        }
    }
    return -1;
}

// 获取工序所在的机器和位置编号
void GetMachineIdAndItemIdByProcess(const std::vector<Schedule_item> &schedule_items, const std::string &process,int type, int &x, int &y) {
    for (int i=0;i<schedule_items.size();i++) {
        for (int j=0;j<schedule_items[i].process_count;j++) {
            if (type==1) {
                if (process == "job"+std::to_string(schedule_items[i].schedule_process[j].job_id)+"-"+std::to_string(schedule_items[i].schedule_process[j].process_id)) {
                    x = i;
                    y = j;
                    return;
                }
            } else if (type==2) {
                if (process == std::to_string(schedule_items[i].schedule_process[j].job_id)+"-"+std::to_string(schedule_items[i].schedule_process[j].process_id)) {
                    x = i;
                    y = j;
                    return;
                }
            }

        }
    }
}

// 迁移邻域搜索，基于工序的类型和所在机器的工序数量的差值进行工序迁移
Schedule MoveNeighborSearch(const Schedule &schedule, const std::vector<Job> &jobs, const std::vector<std::string> &jobList, std::vector<std::string> &processlist, bool &flag, std::vector<Machine> &machines) {
    Schedule res_schedule = schedule;
    res_schedule.set_schedule_id(-1);

    const std::vector<Schedule_item> original_schedule_items = schedule.get_schedule_items();
    std::vector<Schedule_item> schedule_items = original_schedule_items;
    const auto process_location_map = BuildProcessLocationMap(original_schedule_items, false);
    const auto job_id_map = BuildJobIdMap(jobs);
    const auto machine_process_count_map = BuildMachineProcessCountMap(original_schedule_items);
    const int original_total_time = schedule.get_TotalTime();
    // TODO: 迁移邻域搜索，基于工序的类型和所在机器的工序数量的差值进行工序迁移，返回调整后的调度方案
    // for (int i =0;i< processlist.size();i++) {
    //     std::cout << processlist[i] << " ";
    // }
    // std::cout << std::endl;
    int machine_id = -1, item_id = -1, job_id = 0, process_id = 0;
    std::string temp_process = "";
    for (const auto &process: processlist) {
        const auto location_it = process_location_map.find(process);
        if (location_it == process_location_map.end()) {
            continue;
        }
        machine_id = location_it->second.machine_index;
        item_id = location_it->second.item_index;
        // std::cout << process << ":" << machine_id << " " << item_id << std::endl;
        if (item_id>0) {
            job_id = schedule_items[machine_id].schedule_process[item_id].job_id;
            process_id = schedule_items[machine_id].schedule_process[item_id].process_id;

            const auto job_it = job_id_map.find(job_id);
            if (job_it == job_id_map.end()) {
                continue;
            }
            const Job_process temp_job_process = job_it->second->get_job_process()[process_id];
            // for (int i =0;i< temp_job_process.machine_count;i++) {
            //     std::cout << temp_job_process.process_item[i].machine_id << " ";
            // }
            // std::cout << std::endl;

            int min_process_count = schedule_items[machine_id].process_count, temp_machine_id = machine_id;
            int machine_id_1 = 0, process_count = 0;
            for (int i =0; i < temp_job_process.machine_count; i++) {
                machine_id_1 = temp_job_process.process_item[i].machine_id;
                const auto process_count_it = machine_process_count_map.find(machine_id_1);
                process_count = process_count_it == machine_process_count_map.end() ? 0 : process_count_it->second;
                if (process_count < min_process_count) {
                    min_process_count = process_count;
                    temp_machine_id = machine_id_1;
                }
            }
            if (machine_id == temp_machine_id) continue;
            // 将该工序从原机器上移除，并添加至目标机器的合适位置
            schedule_items[machine_id].schedule_process.erase(schedule_items[machine_id].schedule_process.begin()+item_id);
            schedule_items[machine_id].process_count--;

            double min_failure_rate = machines[temp_machine_id].get_failure_rate();
            for (int i = 0; i < temp_job_process.machine_count; i++){
                machine_id_1 = temp_job_process.process_item[i].machine_id;
                double temp_failure_rate = machines[machine_id_1].get_failure_rate();
                if(temp_failure_rate < min_failure_rate) {
                    min_failure_rate = temp_failure_rate;
                    temp_machine_id = machine_id_1;
                }
            }

            int temp_item_i = 0;
            for (int i =0; i< schedule_items[temp_machine_id].process_count; i++) {
                if (schedule_items[temp_machine_id].schedule_process[i].job_id == job_id && schedule_items[temp_machine_id].schedule_process[i].process_id < process_id) {
                    temp_item_i = i+1;
                }
            }

            schedule_items[temp_machine_id].schedule_process.insert(schedule_items[temp_machine_id].schedule_process.begin()+temp_item_i, Schedule_process{job_id, process_id});
            schedule_items[temp_machine_id].process_count++;
            std::vector<std::vector<int>> graph1 = ScheduleItemsToGraph(res_schedule, schedule_items, jobs, jobList, false, machines);
            // std::cout << "now graph status: " << hasCycle(graph1) << std::endl;
            // std::cout << "old time: " << schedule.get_TotalTime() << ", new time: " << CalculateTotalTime(res_schedule) << std::endl;
            if (!hasCycle(graph1)) {
                res_schedule.set_graph(graph1);
                if (CalculateTotalTime(res_schedule) <= original_total_time) {
                    FinalizeAcceptedSchedule(res_schedule, schedule, schedule_items, graph1, machines);
                // std::cout <<"\n=====move "<< job_id << "-" << process_id <<" from machine" << machine_id << " to machine" << temp_machine_id << std::endl;
                    temp_process = process;
                    flag = true;
                    break;
                }
            }
            schedule_items = original_schedule_items;
        }
    }
    if (temp_process != "")
        processlist.erase(std::remove(processlist.begin(), processlist.end(), temp_process), processlist.end());
    return res_schedule;
}

int GetStartTimeByProcess(const Schedule &schedule, const int job_id, const int process_id) {
    for (int i =0; i<schedule.get_processList().size(); i++) {
        if (process_id==0) return 0;
        if (schedule.get_processList()[i] == std::to_string(job_id) + "-" + std::to_string(process_id)) {
            return schedule.get_start_time()[i];
        }
    }
    return 0;
}

// 迁移评分函数，根据当前调度方案中工序的类型和所在机器的工序数量，计算优先级，返回工序列表
std::vector<std::string> ScoreProcessForMove(const Schedule &schedule, const std::vector<Job> &jobs, const std::vector<std::string> &jobList) {
    std::vector<std::string> res_processes;
    const auto job_name_map = BuildJobNameMap(jobs);
    const auto machine_process_count_map = BuildMachineProcessCountMap(schedule.get_schedule_items());
    std::vector<int> job_id_list;
    std::vector<int> process_id_list;
    for (int  i =0 ;i< jobList.size();i++) {
        const auto job_it = job_name_map.find(jobList[i]);
        if (job_it == job_name_map.end()) {
            continue;
        }
        const Job &job = *job_it->second;
        for (int j =0; j< job.get_job_process().size();j++) {
            if (job.get_job_process()[j].machine_count>1) {
                job_id_list.push_back(job.get_job_id());
                process_id_list.push_back(job.get_job_process()[j].process_item[0].process_id);
            }
        }
    }
    const auto job_id_map = BuildJobIdMap(jobs);
    std::vector<std::pair<std::string, int>> temp_vec;
    temp_vec.reserve(job_id_list.size());
    int differ = 0;
    for (int i =0;i< job_id_list.size();i++) {
        const auto current_job_it = job_id_map.find(job_id_list[i]);
        if (current_job_it == job_id_map.end()) {
            continue;
        }
        const Job_process temp_job_process = current_job_it->second->get_job_process()[process_id_list[i]];
        int min_process_count = INT_MAX;
        int now_process_count = 0;
        for (const auto &schedule_item : schedule.get_schedule_items()) {
            for (const auto &schedule_process : schedule_item.schedule_process) {
                if (schedule_process.job_id == job_id_list[i] && schedule_process.process_id == process_id_list[i]) {
                    now_process_count = schedule_item.process_count;
                    break;
                }
            }
            if (now_process_count != 0) {
                break;
            }
        }
        for (int j = 0; j < temp_job_process.machine_count; ++j) {
            const int machine_id = temp_job_process.process_item[j].machine_id;
            const auto process_count_it = machine_process_count_map.find(machine_id);
            const int candidate_count = process_count_it == machine_process_count_map.end() ? 0 : process_count_it->second;
            min_process_count = std::min(min_process_count, candidate_count);
        }
        differ = now_process_count - min_process_count;
        if (differ != 0) {
            temp_vec.emplace_back(std::to_string(job_id_list[i]) + "-" + std::to_string(process_id_list[i]), differ);
        }
    }
    // 按照工序的deffer从大到小排序
    std::sort(temp_vec.begin(), temp_vec.end(), [](const std::pair<std::string, int> &a, const std::pair<std::string, int> &b) {
        return a.second > b.second;
    });
    // 将排序后的工序名称存入res_processes中
    res_processes.reserve(temp_vec.size());
    for (const auto &item : temp_vec) {
        res_processes.push_back(item.first);
    }
    return res_processes;
}

// 计算可调度工序的各个机器的工序数量，返回最大差值
int GetDifferFromIdealMachine(const Schedule &schedule, const std::vector<Job> &jobs, const int job_id, const int process_id) {
    Job_process temp_job_process = SelectJobByJobId(jobs, job_id).get_job_process()[process_id];
    int min_process_count = INT_MAX;
    int machine_id = 0, process_count = 0, now_process_count = 0;
    for (int i =0; i < temp_job_process.machine_count; i++) {
        machine_id = temp_job_process.process_item[i].machine_id;
        process_count = GetProcessCountByMachineId(schedule, machine_id);
        if (process_count < min_process_count) {
            min_process_count = process_count;
        }
    }
    // 获取当前调度方案下，该工序所在机器的工序数量
    for (int i =0 ; i<schedule.get_machine_count(); i++) {
        for (int j = 0; j<schedule.get_schedule_items()[i].process_count; j++) {
            if (schedule.get_schedule_items()[i].schedule_process[j].job_id == job_id && schedule.get_schedule_items()[i].schedule_process[j].process_id == process_id) {
                now_process_count = schedule.get_schedule_items()[i].process_count;
                break;
            }
        }
    }
    return now_process_count - min_process_count;
}

// 获取机器上的工序数量
int GetProcessCountByMachineId(const Schedule &schedule, const int machine_id) {
    for (int i = 0; i<schedule.get_schedule_items().size(); i++) {
        if (schedule.get_schedule_items()[i].machine_id == machine_id) {
            return schedule.get_schedule_items()[i].process_count;
        }
    }
    return 0;
}

// 根据工件编号，获取工件
Job SelectJobByJobId(const std::vector<Job> &jobs, const int job_id) {
    for (auto job: jobs) {
        if (job.get_job_id() == job_id) {
            return job;
        }
    }
    return Job();
}