//
// Created by lpw on 25-4-14.
//

#include "TabuSearch.h"

#include <algorithm>
#include <iostream>
#include <deque>
#include <unordered_map>
#include <unordered_set>

#include "DataProc.h"
#include "GreedySearch.h"
#include "Job.h"
#include "Schedule.h"

namespace {

struct ProcessLocation {
    int machine_index;
    int item_index;
};

struct ParsedProcess {
    std::string process_key;
    int job_id;
    int process_id;
};

std::unordered_map<std::string, ProcessLocation> BuildProcessLocationMap(const std::vector<Schedule_item> &schedule_items) {
    std::unordered_map<std::string, ProcessLocation> process_location_map;
    int process_count = 0;
    for (const auto &item : schedule_items) {
        process_count += item.process_count;
    }
    process_location_map.reserve(process_count);
    for (int machine_index = 0; machine_index < schedule_items.size(); ++machine_index) {
        const auto &processes = schedule_items[machine_index].schedule_process;
        for (int item_index = 0; item_index < processes.size(); ++item_index) {
            const auto &process = processes[item_index];
            process_location_map[std::to_string(process.job_id) + "-" + std::to_string(process.process_id)] = {
                machine_index,
                item_index
            };
        }
    }
    return process_location_map;
}

std::unordered_map<int, const Job *> BuildJobIdMap(const std::vector<Job> &jobs) {
    std::unordered_map<int, const Job *> job_id_map;
    job_id_map.reserve(jobs.size());
    for (const auto &job : jobs) {
        job_id_map[job.get_job_id()] = &job;
    }
    return job_id_map;
}

std::vector<ParsedProcess> BuildParsedKeyProcesses(const std::vector<std::string> &key_process) {
    std::vector<ParsedProcess> parsed_processes;
    if (key_process.size() <= 2) {
        return parsed_processes;
    }
    parsed_processes.reserve(key_process.size() - 2);
    for (int i = 1; i < key_process.size() - 1; ++i) {
        const std::string &process = key_process[i];
        const size_t split_pos = process.find('-');
        parsed_processes.push_back({
            process,
            std::stoi(process.substr(0, split_pos)),
            std::stoi(process.substr(split_pos + 1))
        });
    }
    return parsed_processes;
}

void FinalizeCandidateSchedule(
    Schedule &schedule,
    const Schedule &base_schedule,
    const std::vector<Schedule_item> &schedule_items,
    const std::vector<std::vector<int>> &graph,
    std::vector<Machine> &machines
) {
    schedule.set_schedule_id(base_schedule.get_schedule_id() + 1);
    schedule.set_machine_count(base_schedule.get_machine_count());
    schedule.set_schedule_items(schedule_items);
    schedule.set_graph(graph);
    std::vector<std::string> process_list;
    int process_count = 2;
    for (const auto &item : schedule_items) {
        process_count += item.process_count;
    }
    process_list.reserve(process_count);
    process_list.push_back("start");
    for (const auto &item : schedule_items) {
        for (const auto &process : item.schedule_process) {
            process_list.push_back(std::to_string(process.job_id) + "-" + std::to_string(process.process_id));
        }
    }
    process_list.push_back("end");
    schedule.set_processList(process_list);
    CalculateTotalTime(schedule);
    CalculateTotalFailureRate(schedule, machines);
}

}  // namespace

Schedule TabuSearch(const Schedule &schedule, const std::vector<Job> &jobs, const std::vector<std::string> &jobList, int tabu_list_length, int max_iter_count, std::vector<Machine> &machines) {
    std::deque<std::string> tabu_queue;
    std::unordered_set<std::string> tabu_set;
    tabu_set.reserve(std::max(16, tabu_list_length * 2));
    std::vector<Schedule> scheduleList;
    Schedule best_schedule = schedule;
    int best_total_time = schedule.get_TotalTime();
    Schedule start_schedule = schedule;

    for (int i =0;i<max_iter_count;i++) {
        scheduleList.clear();
        // std::cout << "start_schedule_total_time: " << start_schedule.get_TotalTime() << std::endl;
        std::vector<Schedule> temp_schedules = MoveProcessSearch(start_schedule, jobs, jobList, machines);
        scheduleList.reserve(scheduleList.size() + temp_schedules.size() + 8);
        scheduleList.insert(scheduleList.end(), temp_schedules.begin(), temp_schedules.end());
        temp_schedules = ExchangeProcessSearch(start_schedule, jobs, jobList, machines);
        scheduleList.insert(scheduleList.end(), temp_schedules.begin(), temp_schedules.end());
        if (scheduleList.empty()) {
            break;
        }

        Schedule min_schedule = scheduleList[0];
        int min_total_time = scheduleList[0].get_TotalTime();
        for (const auto &temp_schedule: scheduleList) {
            if (temp_schedule.get_TotalTime() < min_total_time) {
                min_total_time = temp_schedule.get_TotalTime();
                min_schedule = temp_schedule;
            }
        }
        // std::cout << "1------min_total_time: " << min_total_time << "   " << "best_total_time: " << best_total_time << std::endl;
        if (min_total_time < best_total_time) {
            best_total_time = min_total_time;
            best_schedule = min_schedule;
        } else {
            min_schedule = scheduleList[0];
            min_total_time = scheduleList[0].get_TotalTime();
            for (const auto &temp_schedule: scheduleList) {
                if (!IsInTabuList(temp_schedule, tabu_set) && temp_schedule.get_TotalTime() < min_total_time) {
                    min_total_time = temp_schedule.get_TotalTime();
                    min_schedule = temp_schedule;
                }
            }
            // std::cout << "2------min_total_time: " << min_total_time << "   " << "best_total_time: " << best_total_time << std::endl;
            best_total_time = min_total_time;
            best_schedule = min_schedule;
        }
        AddScheduleToTabuList(start_schedule, tabu_queue, tabu_set, tabu_list_length);
        start_schedule = best_schedule;
        // std::cout << "iter_count: " << i << ", best_total_time: " << best_total_time << std::endl;
    }
    return best_schedule;
}

std::vector<Schedule> MoveProcessSearch(const Schedule &schedule, const std::vector<Job> &jobs, const std::vector<std::string> &jobList, std::vector<Machine> &machines) {
    std::vector<Schedule> scheduleList;
    Schedule res_schedule = schedule;
    res_schedule.set_schedule_id(-1);
    const std::vector<std::string> key_process = schedule.GetKeyProcess();
    const std::vector<Schedule_item> schedule_items = schedule.get_schedule_items();
    const auto process_location_map = BuildProcessLocationMap(schedule_items);
    const auto job_id_map = BuildJobIdMap(jobs);
    const auto parsed_key_processes = BuildParsedKeyProcesses(key_process);
    scheduleList.reserve(parsed_key_processes.size() * 2);

    int machine_id = -1, item_id = -1, temp_item_i = 0;
    for (const auto &parsed_process : parsed_key_processes) {
        // std::cout << "now:"<<key_process[k] << std::endl;
        const auto location_it = process_location_map.find(parsed_process.process_key);
        if (location_it == process_location_map.end()) {
            continue;
        }
        machine_id = location_it->second.machine_index;
        item_id = location_it->second.item_index;
        const int job_id = parsed_process.job_id;
        const int process_id = parsed_process.process_id;
        const auto job_it = job_id_map.find(job_id);
        if (job_it == job_id_map.end()) {
            continue;
        }
        const Job_process &temp_job_process = job_it->second->get_job_process()[process_id];

        for (const auto &process_item: temp_job_process.process_item) {
            if (process_item.machine_id != machine_id) {
                int temp_machine_i = process_item.machine_id;
                std::vector<Schedule_item> temp_schedule_items = schedule_items;
                temp_schedule_items[machine_id].schedule_process.erase(temp_schedule_items[machine_id].schedule_process.begin()+item_id);
                temp_schedule_items[machine_id].process_count--;

                temp_item_i = 0;
                for (int i =0; i< temp_schedule_items[temp_machine_i].process_count; i++) {
                    if (temp_schedule_items[temp_machine_i].schedule_process[i].job_id == job_id && temp_schedule_items[temp_machine_i].schedule_process[i].process_id < process_id) {
                        temp_item_i = i+1;
                    }
                }
                temp_schedule_items[temp_machine_i].schedule_process.insert(temp_schedule_items[temp_machine_i].schedule_process.begin()+temp_item_i, Schedule_process{job_id, process_id});
                temp_schedule_items[temp_machine_i].process_count++;
                // std::cout << "move process: " << process << " from machine" << machine_id << " to machine" << temp_machine_i << std::endl;
                std::vector<std::vector<int>> graph1 = ScheduleItemsToGraph(res_schedule, temp_schedule_items, jobs, jobList, false, machines);
                if (!hasCycle(graph1)) {
                    FinalizeCandidateSchedule(res_schedule, schedule, temp_schedule_items, graph1, machines);
                    scheduleList.push_back(res_schedule);
                }
            }
        }
    }
    return scheduleList;
}

std::vector<Schedule> ExchangeProcessSearch(const Schedule &schedule, const std::vector<Job> &jobs, const std::vector<std::string> &jobList, std::vector<Machine> &machines) {
    std::vector<Schedule> scheduleList;
    Schedule res_schedule = schedule;
    res_schedule.set_schedule_id(-1);
    const std::vector<std::string> key_process = schedule.GetKeyProcess();
    const std::vector<Schedule_item> schedule_items = schedule.get_schedule_items();
    const auto process_location_map = BuildProcessLocationMap(schedule_items);
    const auto job_id_map = BuildJobIdMap(jobs);
    const auto parsed_key_processes = BuildParsedKeyProcesses(key_process);
    scheduleList.reserve(parsed_key_processes.size());

    int machine_id = -1, item_id = -1;
    for (const auto &parsed_process : parsed_key_processes) {
        // std::cout << "now:"<<key_process[k] << std::endl;
        const auto location_it = process_location_map.find(parsed_process.process_key);
        if (location_it == process_location_map.end()) {
            continue;
        }
        machine_id = location_it->second.machine_index;
        item_id = location_it->second.item_index;
        const int job_id = parsed_process.job_id;
        const int process_id = parsed_process.process_id;
        const auto job_it = job_id_map.find(job_id);
        if (job_it == job_id_map.end()) {
            continue;
        }

        std::vector<Schedule_item> temp_schedule_items = schedule_items;
        int temp_item = item_id;
        for (int i = item_id-1; i>=0; i--) {
            if (temp_schedule_items[machine_id].schedule_process[i].job_id == job_id) {
                temp_item = i;
                break;

            }
        }
        if (temp_item != item_id) {
            temp_schedule_items[machine_id].schedule_process.erase(temp_schedule_items[machine_id].schedule_process.begin()+item_id);
            temp_schedule_items[machine_id].schedule_process.insert(temp_schedule_items[machine_id].schedule_process.begin()+temp_item, Schedule_process{job_id, process_id});
        }
        // std::cout << "exchange process: " << process << " with previous process" << std::endl;
        std::vector<std::vector<int>> graph1 = ScheduleItemsToGraph(res_schedule, temp_schedule_items, jobs, jobList, false, machines);
        if (!hasCycle(graph1)) {
            FinalizeCandidateSchedule(res_schedule, schedule, temp_schedule_items, graph1, machines);
            scheduleList.push_back(res_schedule);
        }
    }
    return scheduleList;
}

std::string ScheduleToString(const Schedule &schedule) {
    std::string res;
    const auto &schedule_items = schedule.get_schedule_items();
    size_t estimated_size = 0;
    for (const auto &schedule_item : schedule_items) {
        estimated_size += 12;  // machine_id + ":" + buffer
        estimated_size += static_cast<size_t>(schedule_item.process_count) * 24;  // job_id-process_id, + ","
    }
    res.reserve(estimated_size);
    for (const auto &schedule_item: schedule_items) {
        res += std::to_string(schedule_item.machine_id) + ":";
        for (const auto &process: schedule_item.schedule_process) {
            res += std::to_string(process.job_id) + "-" + std::to_string(process.process_id) + ",";
        }
    }
    return res;
}

bool IsInTabuList(const Schedule &schedule, const std::unordered_set<std::string> &tabu_set) {
    const std::string schedule_str = ScheduleToString(schedule);
    return tabu_set.find(schedule_str) != tabu_set.end();
}

void AddScheduleToTabuList(const Schedule &schedule, std::deque<std::string> &tabu_queue, std::unordered_set<std::string> &tabu_set, const int tabu_list_length) {
    const std::string schedule_str = ScheduleToString(schedule);
    if (tabu_list_length <= 0) {
        return;
    }
    if (tabu_queue.size() >= tabu_list_length) {
        tabu_set.erase(tabu_queue.front());
        tabu_queue.pop_front();
    }
    tabu_queue.push_back(schedule_str);
    tabu_set.insert(schedule_str);
}
