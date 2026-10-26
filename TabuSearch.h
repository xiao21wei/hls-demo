//
// Created by 28898 on 25-4-14.
//

#ifndef TABUSEARCH_H
#define TABUSEARCH_H
#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

#include "Job.h"
#include "Schedule.h"
#include "Machine.h"

// Tabu Search, 禁忌搜索算法
Schedule TabuSearch(const Schedule &schedule, const std::vector<Job> &jobs, const std::vector<std::string> &jobList, int tabu_list_length, int max_iter_count, std::vector<Machine> &machines);

// 迁移邻域搜索，基于机器重分配进行工序迁移
std::vector<Schedule> MoveProcessSearch(const Schedule &schedule, const std::vector<Job> &jobs, const std::vector<std::string> &jobList, std::vector<Machine> &machines);

// 交换邻域搜索，基于工序前移进行工序交换
std::vector<Schedule> ExchangeProcessSearch(const Schedule &schedule, const std::vector<Job> &jobs, const std::vector<std::string> &jobList, std::vector<Machine> &machines);

// 获取调度方案的字符串表示
std::string ScheduleToString(const Schedule &schedule);

// 判断调度方案是否在禁忌列表中
bool IsInTabuList(const Schedule &schedule, const std::unordered_set<std::string> &tabu_set);

// 添加调度方案到禁忌列表
void AddScheduleToTabuList(const Schedule &schedule, std::deque<std::string> &tabu_queue, std::unordered_set<std::string> &tabu_set, int tabu_list_length);
#endif //TABUSEARCH_H
