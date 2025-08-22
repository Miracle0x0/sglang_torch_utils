# /// script
# requires-python = ">=3.11"
# dependencies = [
#     "orjson",
#     "typer",
# ]
# ///

import os
import gzip
import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import List, Dict, Optional

import orjson
import typer

profile_utils = typer.Typer()


@profile_utils.command()
def convert(filepath: str):
    dir_data = os.path.dirname(filepath)
    filename = os.path.basename(filepath)
    path_input = os.path.join(dir_data, filename)
    path_output = os.path.join(dir_data, f"perfetto-compatible-{filename}")
    print(f"=== {path_input=} {path_output=}")
    if not os.path.exists(path_input):
        raise FileNotFoundError(f"Input {path_input} not found.")

    def _process_events(events: List):
        print(f"=== process_events start {len(events)=}")

        _is_interest_event = lambda e: "registers per thread" in e.get("args", {})  # noqa

        last_end_time_of_pid_tid = defaultdict(lambda: -1)
        for e in events:
            if e["ph"] == "X" and _is_interest_event(e):
                while e["ts"] < last_end_time_of_pid_tid[(e["pid"], e["tid"])]:
                    e["tid"] = str(e["tid"]) + "_hack"
                last_end_time_of_pid_tid[(e["pid"], e["tid"])] = e["ts"] + e["dur"]
        return events

    with gzip.open(path_input, "rt", encoding="utf-8") as f:
        trace = orjson.loads(f.read())
        output = {k: v for k, v in trace.items() if k != "traceEvents"}
        output["traceEvents"] = _process_events(trace.get("traceEvents", []))

    with gzip.open(path_output, "wb") as f:
        f.write(orjson.dumps(output))

    print("=== Convert finished.")


@dataclass
class Config:
    start_time_ms: int
    end_time_ms: int
    thread_filters: str


def _merge_chrome_traces(
    interesting_paths: List[Path],
    output_path: Path,
    config: Config,
):
    merged_trace = {"traceEvents": []}
    for output_raw in map(
        _handle_file, interesting_paths, [config] * len(interesting_paths)
    ):
        merged_trace["traceEvents"].extend(output_raw["traceEvents"])
        for k, v in output_raw.items():
            if k != "traceEvents" and k not in merged_trace:
                merged_trace[k] = v

    print(f"=== Write output to {output_path}")
    with gzip.open(output_path, "wb") as f:
        f.write(orjson.dumps(merged_trace))


def _handle_file(path, config: Config):
    print(f"=== handle_file START {path=}")

    _get_tp_rank_of_path = lambda p: int(re.search("TP-(\d+)", p.name).group(1))  # noqa
    tp_rank = _get_tp_rank_of_path(path)

    with gzip.open(path, "rt", encoding="utf-8") as f:
        trace = orjson.loads(f.read())
        print(f"=== handle_file {path=} {tp_rank=} {list(trace)=}")

        output = {k: v for k, v in trace.items() if k != "traceEvents"}
        output["traceEvents"] = _process_tp_events(
            trace.get("traceEvents", []), config, tp_rank
        )

    print(f"=== handle_file END {path=}")
    return output


def _process_tp_events(events: List[Dict], config: Config, tp_rank: int):
    print(f"=== {len(events)}")

    # format: us
    min_ts = min(e["ts"] for e in events)
    ts_interest_start = min_ts + 1000 * config.start_time_ms
    ts_interest_end = min_ts + 1000 * config.end_time_ms
    events = [e for e in events if ts_interest_start <= e["ts"] <= ts_interest_end]

    print(
        f"=== after filtering by timestamp {len(events)=} ({ts_interest_start=} {ts_interest_end=})"
    )

    if config.thread_filters is not None:
        thread_filters_list = config.thread_filters.split(",")

        thread_name_of_tid = {
            e["tid"]: e["args"]["name"] for e in events if e["name"] == "thread_name"
        }

        def _thread_name_filter_fn(thread_id):
            ans = False
            if "gpu" in thread_filters_list:
                ans |= "stream" in str(thread_id)
            return ans

        remove_tids = [
            tid
            for tid, thread_name in thread_name_of_tid.items()
            if not _thread_name_filter_fn(thread_name)
        ]
        print(f"=== {remove_tids=}")

        events = [e for e in events if e["tid"] not in remove_tids]
        print(f"=== after filtering by thread_filters {len(events)=}")

    def _maybe_cast_int(x):
        try:
            return int(x)
        except ValueError:
            return None

    for e in events:
        if e["name"] == "process_sort_index":
            pid = _maybe_cast_int(e["pid"])
            if pid is not None and pid < 1000:
                e["args"]["sort_index"] = 100 * tp_rank + int(e["pid"])
        # modify in place to speed up
        e["pid"] = f"[TP{tp_rank:02d} {e['pid']}]"

    return events


@profile_utils.command()
def merge(
    dir_data: str,
    profile_id: Optional[str] = None,
    start_time_ms: Optional[int] = 0,
    end_time_ms: Optional[int] = 999999999,
    thread_filters: str = None,
    perfetto_compatible: bool = False,
):
    """Merge multiple Torch Profiler traces from multiple ranks into one big trace (useful when checking cooperation between ranks).

    Args:
        profile_id (Optional[str], optional)
        start_time_ms (Optional[int], optional)
        end_time_ms (Optional[int], optional)
        thread_filters (str, optional)
        dir_data: (str): Path to the traces to be merged.
        perfetto_compatible (bool, optional): Whether to merge perfetto compatible traces. Defaults to False.
    """
    dir_data = Path(dir_data)

    if profile_id is None:
        pattern = (
            re.compile(r"^([0-9.]+)-.*$")
            if not perfetto_compatible
            else re.compile(r"^perfetto-compatible-([0-9.]+)-.*$")
        )
        profile_id = max(
            m.group(1)
            for p in dir_data.glob("*.json.gz")
            if (m := pattern.match(p.name)) is not None
        )
    print(f"{profile_id=}")

    _get_tp_rank_of_path = lambda p: int(re.search("TP-(\d+)", p.name).group(1))  # noqa
    interesting_paths = sorted(
        # [p for p in dir_data.glob("*.json.gz") if p.name.startswith(profile_id)],
        [p for p in dir_data.glob("*.json.gz") if p.name.startswith(profile_id)],
        key=_get_tp_rank_of_path,
    )
    print(f"=== {interesting_paths=}")

    if perfetto_compatible:
        output_path = (
            dir_data / f"merged-perfetto-compatible-{profile_id}.trace.json.gz"
        )
    else:
        output_path = dir_data / f"merged-{profile_id}.trace.json.gz"
    _merge_chrome_traces(
        interesting_paths,
        output_path,
        config=Config(start_time_ms, end_time_ms, thread_filters),
    )

    print("=== Merge finished.")


if __name__ == "__main__":
    profile_utils()
