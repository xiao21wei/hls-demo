#!/usr/bin/env python3
"""Run HLS-FJSP ten times on every Barnes instance with adaptive concurrency."""

import argparse
import csv
import json
import math
import os
from pathlib import Path
import re
import signal
import statistics
import subprocess
import sys
import time
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parent
HLS_ROOT = Path(os.environ.get("HLS_ROOT", str(ROOT.parent / "HLS"))).resolve()
if str(HLS_ROOT) not in sys.path:
    sys.path.insert(0, str(HLS_ROOT))

import run_adaptive_brandimarte as resource_batch


DATASET_DIR = ROOT / "datasets" / "SmartLab_Challenge_2020"
INSTANCE_PATTERN = re.compile(r"^fjsp\.barnes\.([^.]+)\..+\.txt$")


def natural_key(value):
    return [int(part) if part.isdigit() else part.lower()
            for part in re.split(r"(\d+)", value)]


def atomic_json(path, value):
    resource_batch.atomic_json(Path(path), value)


def write_csv(path, rows):
    resource_batch.write_csv(Path(path), rows)


def discover_instances():
    instances = {}
    for path in DATASET_DIR.glob("fjsp.barnes.*.txt"):
        match = INSTANCE_PATTERN.match(path.name)
        if not match:
            continue
        label = match.group(1)
        if label in instances:
            raise ValueError("duplicate Barnes label: %s" % label)
        instances[label] = path.resolve()
    if len(instances) != 21:
        raise ValueError("expected 21 Barnes instances, found %d" % len(instances))
    return [(label, instances[label])
            for label in sorted(instances, key=natural_key)]


def parse_result(log_path):
    lines = [line.strip() for line in
             log_path.read_text(encoding="utf-8", errors="replace").splitlines()
             if line.strip()]
    if len(lines) < 2 or not lines[-2].isdigit() or not lines[-1].isdigit():
        raise ValueError("final makespan/runtime lines are missing")
    makespan = int(lines[-2])
    solver_seconds = int(lines[-1])
    if makespan <= 0 or solver_seconds < 0:
        raise ValueError("invalid final makespan/runtime")
    return makespan, solver_seconds


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", type=int, default=int(os.environ.get("RUNS", "10")))
    parser.add_argument("--output-dir", default=os.environ.get("OUTPUT_DIR"))
    parser.add_argument("--binary", default=os.environ.get(
        "PRODUCTION_SCHEDULING_BIN", str(ROOT / "build" / "production_scheduling")))
    parser.add_argument("--max-parallel", type=int,
                        default=int(os.environ.get("MAX_PARALLEL", "20")))
    parser.add_argument("--cpu-usage-limit", type=float,
                        default=float(os.environ.get("CPU_USAGE_LIMIT", "0.92")))
    parser.add_argument("--reserve-cpus", type=float,
                        default=float(os.environ.get("CPU_RESERVE", "1")))
    parser.add_argument("--job-memory-gb", type=float,
                        default=float(os.environ.get("JOB_MEMORY_GB", "0.25")))
    parser.add_argument("--reserve-memory-gb", type=float,
                        default=float(os.environ.get("MEMORY_RESERVE_GB", "4")))
    parser.add_argument("--check-interval", type=float,
                        default=float(os.environ.get("CHECK_INTERVAL", "2")))
    parser.add_argument("--launch-spacing", type=float,
                        default=float(os.environ.get("LAUNCH_SPACING", "1.05")))
    parser.add_argument("--task-timeout", type=float,
                        default=float(os.environ.get("TASK_TIMEOUT", "3700")))
    parser.add_argument("--search-mode-param", type=float, default=0.5)
    parser.add_argument("--random-search-strategy-param", type=float, default=0.5)
    parser.add_argument("--max-repeat-count", type=int, default=100)
    parser.add_argument("--max-iter-count", type=int, default=30)
    parser.add_argument("--population-size", type=int, default=20)
    parser.add_argument("--tabu-list-length", type=int, default=25)
    parser.add_argument("--algorithm-mode", default="HLS",
                        choices=("HLS", "GS", "GA", "TS", "GSGA", "GSTS", "GATS"))
    args = parser.parse_args()
    if args.runs < 1 or args.max_parallel < 1:
        parser.error("runs and max-parallel must be positive")
    if not 0.1 <= args.cpu_usage_limit <= 1.0:
        parser.error("cpu-usage-limit must be between 0.1 and 1")
    if args.job_memory_gb <= 0 or args.reserve_memory_gb < 0:
        parser.error("invalid memory setting")
    if args.check_interval < 0.1 or args.launch_spacing < 1.0:
        parser.error("check-interval must be >= 0.1 and launch-spacing >= 1.0")
    if args.task_timeout <= 0:
        parser.error("task-timeout must be positive")
    binary = Path(args.binary).resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error("binary is missing or not executable: %s" % binary)
    args.binary = str(binary)
    return args


class Runner:
    def __init__(self, directory, settings, instances):
        self.directory = directory
        self.settings = settings
        self.instances = instances
        self.resources = resource_batch.Resources()
        self.active = {}
        self.stop = False
        self.stop_signal = 0
        self.limit = 0
        self.cpu_cursor = 0
        self.peak_rss = 0
        self.next_launch = 0.0
        self.last_launch_epoch = None
        self.logs = directory / "logs"
        self.runs = directory / "runs"
        self.logs.mkdir()
        self.runs.mkdir()
        self.scheduler = (directory / "scheduler.log").open(
            "a", buffering=1, encoding="utf-8")
        self.metrics = (directory / "scheduler.csv").open(
            "a", newline="", buffering=1, encoding="utf-8")
        self.metrics_writer = csv.writer(self.metrics)
        self.metrics_writer.writerow([
            "time", "running", "pending", "ok", "failed", "limit",
            "effective_cpus", "cpu_percent", "external_cores",
            "memory_available_gb", "job_memory_gb", "memory_reserve_gb",
        ])
        self.tasks = []
        for run in range(1, settings.runs + 1):
            for label, path in instances:
                self.tasks.append({
                    "label": label,
                    "input": str(path),
                    "run": run,
                    "name": "%s_run%02d" % (label, run),
                    "status": "PENDING",
                    "attempt": 0,
                })

    def message(self, value):
        line = "[%s] %s" % (resource_batch.now(), value)
        print(line, flush=True)
        self.scheduler.write(line + "\n")

    def save(self):
        pending = any(task["status"] in ("PENDING", "RUNNING")
                      for task in self.tasks)
        all_ok = all(task["status"] == "OK" for task in self.tasks)
        phase = "RUNNING" if pending else "COMPLETE" if all_ok else "FAILED"
        atomic_json(self.directory / "state.json", {
            "phase": phase,
            "peak_rss": self.peak_rss,
            "tasks": self.tasks,
        })
        write_csv(self.directory / "launch_times.csv", [
            ["instance", "run", "launch_unix_second", "launch_time"],
        ] + [[task["label"], task["run"], task.get("launch_epoch", ""),
              task.get("started_at", "")] for task in self.tasks])
        self.write_results(all_ok)

    def write_results(self, publish_root):
        lookup = {(task["label"], task["run"]): task for task in self.tasks}
        rows = [["instance"] + ["run%d" % run
                                 for run in range(1, self.settings.runs + 1)]]
        summary = [[
            "instance", "completed", "expected", "mean", "min", "max",
            "stddev", "mean_solver_seconds", "mean_wall_seconds",
        ]]
        for label, _ in self.instances:
            tasks = [lookup[(label, run)]
                     for run in range(1, self.settings.runs + 1)]
            completed = [task for task in tasks if task["status"] == "OK"]
            values = [task["makespan"] for task in completed]
            rows.append([label] + [
                task.get("makespan", task["status"]) for task in tasks
            ])
            summary.append([
                label,
                len(values),
                self.settings.runs,
                "%.4f" % statistics.mean(values) if values else "",
                min(values) if values else "",
                max(values) if values else "",
                "%.4f" % statistics.stdev(values) if len(values) > 1
                else "0.0000" if values else "",
                "%.3f" % statistics.mean(
                    task["solver_seconds"] for task in completed) if values else "",
                "%.3f" % statistics.mean(
                    task["wall_seconds"] for task in completed) if values else "",
            ])
        result_path = self.directory / "HLS_FJSP_barnes.csv"
        write_csv(result_path, rows)
        write_csv(self.directory / "summary.csv", summary)
        if publish_root:
            write_csv(ROOT / "HLS_FJSP_barnes.csv", rows)

    def request_stop(self, signum, _frame):
        self.stop = True
        self.stop_signal = signum

    def launch(self, task):
        now_monotonic = time.monotonic()
        launch_epoch = int(time.time())
        if now_monotonic < self.next_launch or launch_epoch == self.last_launch_epoch:
            return False
        occupied = {cpu for item in self.active.values() for cpu in item["cpus"]}
        affinity = self.resources.affinity
        rotated = affinity[self.cpu_cursor:] + affinity[:self.cpu_cursor]
        cpus = [cpu for cpu in rotated if cpu not in occupied][:1]
        if not cpus:
            return False
        self.cpu_cursor = (affinity.index(cpus[-1]) + 1) % len(affinity)
        log_path = self.logs / (task["name"] + ".log")
        command = [
            self.settings.binary,
            task["input"],
            str(self.settings.search_mode_param),
            str(self.settings.random_search_strategy_param),
            str(self.settings.max_repeat_count),
            str(self.settings.max_iter_count),
            str(self.settings.population_size),
            str(self.settings.tabu_list_length),
            self.settings.algorithm_mode,
        ]
        environment = os.environ.copy()
        for name in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS",
                     "MKL_NUM_THREADS", "NUMEXPR_NUM_THREADS"):
            environment[name] = "1"
        handle = log_path.open("w", encoding="utf-8")
        try:
            process = subprocess.Popen(
                command,
                cwd=str(ROOT),
                env=environment,
                stdout=handle,
                stderr=subprocess.STDOUT,
                start_new_session=True,
                preexec_fn=lambda: os.sched_setaffinity(0, cpus),
            )
        except Exception:
            handle.close()
            raise
        task.update({
            "status": "RUNNING",
            "attempt": task["attempt"] + 1,
            "pid": process.pid,
            "cpus": " ".join(map(str, cpus)),
            "launch_epoch": launch_epoch,
            "started_at": resource_batch.now(),
        })
        self.active[process.pid] = {
            "process": process,
            "task": task,
            "handle": handle,
            "started": now_monotonic,
            "ending": None,
            "cpus": cpus,
        }
        self.last_launch_epoch = launch_epoch
        self.next_launch = now_monotonic + self.settings.launch_spacing
        self.message("start %s launch_epoch=%d pid=%d CPU=%s" % (
            task["name"], launch_epoch, process.pid, task["cpus"]))
        return True

    @staticmethod
    def terminate(item, reason):
        if item["ending"] is None:
            item["ending"] = (time.monotonic(), reason)
            try:
                os.killpg(item["process"].pid, signal.SIGTERM)
            except ProcessLookupError:
                pass

    def reap(self):
        changed = False
        for pid, item in list(self.active.items()):
            process = item["process"]
            task = item["task"]
            elapsed = time.monotonic() - item["started"]
            if elapsed > self.settings.task_timeout:
                self.terminate(item, "TIMEOUT")
            if item["ending"] and time.monotonic() - item["ending"][0] > 3 \
                    and process.poll() is None:
                try:
                    os.killpg(pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            code = process.poll()
            if code is None:
                continue
            item["handle"].close()
            log_path = self.logs / (task["name"] + ".log")
            status = item["ending"][1] if item["ending"] else "FAILED"
            error = "exit=%s" % code
            if code == 0 and not item["ending"]:
                try:
                    makespan, solver_seconds = parse_result(log_path)
                    if not math.isfinite(makespan):
                        raise ValueError("non-finite makespan")
                    task["makespan"] = makespan
                    task["solver_seconds"] = solver_seconds
                    status = "OK"
                    error = ""
                    atomic_json(self.runs / (task["name"] + ".json"), {
                        "family": "barnes",
                        "instance": task["label"],
                        "input": task["input"],
                        "run": task["run"],
                        "launch_unix_second": task["launch_epoch"],
                        "makespan": makespan,
                        "solver_seconds": solver_seconds,
                        "wall_seconds": round(elapsed, 3),
                    })
                except (OSError, ValueError) as exc:
                    error = str(exc)
            task.update({
                "status": status,
                "exit_code": code,
                "wall_seconds": round(elapsed, 3),
                "finished_at": resource_batch.now(),
                "error": error,
            })
            self.message("%s %s makespan=%s wall=%.1fs" % (
                status.lower(), task["name"], task.get("makespan", "-"), elapsed))
            del self.active[pid]
            changed = True
        return changed

    def report(self, snapshot, job_memory, reserve):
        pending = sum(task["status"] == "PENDING" for task in self.tasks)
        ok = sum(task["status"] == "OK" for task in self.tasks)
        failed = sum(task["status"] in ("FAILED", "TIMEOUT", "INTERRUPTED")
                     for task in self.tasks)
        cpu_percent = 100 * snapshot["busy_cores"] / snapshot["cores"]
        self.metrics_writer.writerow([
            resource_batch.now(), len(self.active), pending, ok, failed,
            self.limit, "%.2f" % snapshot["cores"], "%.2f" % cpu_percent,
            "%.2f" % snapshot["external_cores"],
            "%.2f" % (snapshot["memory_available"] / resource_batch.GIB),
            "%.2f" % (job_memory / resource_batch.GIB),
            "%.2f" % (reserve / resource_batch.GIB),
        ])
        self.message(
            "progress OK=%d/%d failed=%d running=%d limit=%d pending=%d "
            "CPU=%.1f%% memory_available=%.1fGiB" % (
                ok, len(self.tasks), failed, len(self.active), self.limit,
                pending, cpu_percent,
                snapshot["memory_available"] / resource_batch.GIB))

    def execute(self):
        previous = {sig: signal.signal(sig, self.request_stop)
                    for sig in (signal.SIGINT, signal.SIGTERM)}
        self.resources.sample(set())
        next_check = 0.0
        self.save()
        try:
            while True:
                changed = self.reap()
                if self.stop:
                    break
                pending = [task for task in self.tasks
                           if task["status"] == "PENDING"]
                if not pending and not self.active:
                    break
                now_monotonic = time.monotonic()
                if now_monotonic >= next_check:
                    snapshot = self.resources.sample(set(self.active))
                    self.peak_rss = max(
                        [self.peak_rss] + list(snapshot["rss"].values()))
                    admission_settings = SimpleNamespace(
                        cpu_usage_limit=self.settings.cpu_usage_limit,
                        reserve_cpus=self.settings.reserve_cpus,
                        cpus_per_job=1,
                        job_memory_gb=self.settings.job_memory_gb,
                        reserve_memory_gb=self.settings.reserve_memory_gb,
                        max_parallel=self.settings.max_parallel,
                    )
                    self.limit, job_memory, reserve = resource_batch.admission(
                        snapshot, admission_settings, self.peak_rss)
                    self.report(snapshot, job_memory, reserve)
                    next_check = now_monotonic + self.settings.check_interval
                if pending and len(self.active) < self.limit:
                    if self.launch(pending[0]):
                        changed = True
                if changed:
                    self.save()
                time.sleep(0.1)
        finally:
            for item in self.active.values():
                self.terminate(item, "INTERRUPTED")
            while self.active:
                self.reap()
                if self.active:
                    time.sleep(0.1)
            self.save()
            self.scheduler.close()
            self.metrics.close()
            for sig, handler in previous.items():
                signal.signal(sig, handler)
        if self.stop:
            return 128 + self.stop_signal
        return 0 if all(task["status"] == "OK" for task in self.tasks) else 1


def main():
    settings = arguments()
    instances = discover_instances()
    stamp = time.strftime("%Y%m%d_%H%M%S")
    directory = Path(settings.output_dir or (
        ROOT / "result_barnes" / ("barnes_" + stamp))).resolve()
    if directory.exists() and any(directory.iterdir()):
        raise ValueError("output directory is not empty: %s" % directory)
    directory.mkdir(parents=True, exist_ok=True)
    sources = [
        "main.cpp", "DataProc.cpp", "Experiment.cpp", "GreedySearch.cpp",
        "Job.cpp", "Machine.cpp", "Order.cpp", "RandomSearch.cpp",
        "Schedule.cpp", "TabuSearch.cpp", Path(__file__).name,
    ]
    manifest = {
        "schema": 1,
        "created_at": resource_batch.now(),
        "family": "barnes",
        "instances": [label for label, _ in instances],
        "runs": settings.runs,
        "parameters": {
            "search_mode_param": settings.search_mode_param,
            "random_search_strategy_param": settings.random_search_strategy_param,
            "max_repeat_count": settings.max_repeat_count,
            "max_iter_count": settings.max_iter_count,
            "population_size": settings.population_size,
            "tabu_list_length": settings.tabu_list_length,
            "algorithm_mode": settings.algorithm_mode,
        },
        "randomness": (
            "The unmodified executable seeds std::rand with time(nullptr) and "
            "also uses std::random_device. Launches are separated by at least "
            "one second; launch Unix seconds are recorded, but are not an exact "
            "record of every internal RNG seed."
        ),
        "scheduler": vars(settings),
        "source_sha256": {
            name: resource_batch.sha256(ROOT / name) for name in sources
        },
        "binary_sha256": resource_batch.sha256(Path(settings.binary)),
        "dataset_sha256": {
            label: resource_batch.sha256(path) for label, path in instances
        },
    }
    atomic_json(directory / "config.json", manifest)
    runner = Runner(directory, settings, instances)
    runner.message("Run directory: %s" % directory)
    runner.message("Barnes: %d instances x %d runs = %d tasks" % (
        len(instances), settings.runs, len(runner.tasks)))
    result = runner.execute()
    print("Run directory: %s" % directory)
    print("Result CSV: %s" % (directory / "HLS_FJSP_barnes.csv"))
    return result


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print("Error: %s" % error, file=sys.stderr)
        sys.exit(2)
