#!/usr/bin/env python3
"""Benchmark script for VM_Call vs VM_CallArgv comparison.

See call_argv_benchmark.md for the benchmark plan.

Usage:
    python3 run_benchmark.py
"""

import os
import re
import subprocess
import sys
import time

VALKEY_DIR = os.path.dirname(os.path.abspath(__file__))
SERVER_BIN = os.path.join(VALKEY_DIR, "src/valkey-server")
CLI_BIN    = os.path.join(VALKEY_DIR, "src/valkey-cli")
BENCH_BIN  = os.path.join(VALKEY_DIR, "src/valkey-benchmark")
MODULE     = os.path.join(VALKEY_DIR, "tests/modules/call.so")
PORT       = 7399
N_REQUESTS = 300_000
N_CLIENTS  = 50


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def cli(*args):
    r = subprocess.run([CLI_BIN, "-p", str(PORT)] + list(str(a) for a in args),
                       capture_output=True, text=True)
    return r.stdout.strip()


def cli_pipe(commands):
    """Send a batch of commands via inline RESP pipelining."""
    resp = ""
    for cmd in commands:
        resp += f"*{len(cmd)}\r\n"
        for arg in cmd:
            s = str(arg)
            resp += f"${len(s)}\r\n{s}\r\n"
    subprocess.run([CLI_BIN, "-p", str(PORT), "--pipe"],
                   input=resp, capture_output=True, text=True)


def build_module():
    print("Building call.so ...")
    subprocess.run(["make", "-C", os.path.join(VALKEY_DIR, "tests/modules"), "call.so"],
                   check=True, capture_output=True)
    print("  done.")


def start_server():
    print(f"Starting valkey-server on port {PORT} ...")
    subprocess.run([
        SERVER_BIN,
        "--port", str(PORT),
        "--daemonize", "yes",
        "--logfile", "/tmp/valkey-bench.log",
        "--loadmodule", MODULE,
        "--save", "",
    ], check=True)
    # Wait for ready
    for _ in range(20):
        time.sleep(0.2)
        if cli("ping") == "PONG":
            print("  server ready.")
            return
    print("ERROR: server did not respond to PING", file=sys.stderr)
    sys.exit(1)


def stop_server():
    subprocess.run([CLI_BIN, "-p", str(PORT), "shutdown", "nosave"],
                   capture_output=True)
    time.sleep(0.3)
    print("Server stopped.")


def populate():
    print("Populating data ...")

    # Simple key
    cli("set", "mykey", "hello world")

    # Large key (~100KB) for Category E
    cli("set", "mybigkey", "x" * 100_000)

    # 100 individual keys for EXISTS benchmark (category B)
    mset_cmd = ["mset"]
    for i in range(1, 101):
        mset_cmd += [f"k{i}", f"val{i}"]
    cli(*mset_cmd)

    # Hash: 500 fields in one HSET
    cli("del", "myhash")
    hset_cmd = ["hset", "myhash"]
    for i in range(1, 501):
        hset_cmd += [f"f{i}", f"value{i}"]
    cli(*hset_cmd)

    # List: 500 elements in one RPUSH
    cli("del", "mylist")
    rpush_cmd = ["rpush", "mylist"] + [f"elem{i}" for i in range(1, 501)]
    cli(*rpush_cmd)

    # Stream: 500 entries via pipeline
    cli("del", "mystream")
    cli_pipe([
        ["xadd", "mystream", "*", "field1", f"val{i}", "field2", f"data{i}"]
        for i in range(1, 501)
    ])

    print(f"  mykey  : '{cli('get', 'mykey')}'")
    print(f"  mybigkey: {cli('strlen', 'mybigkey')} bytes")
    print(f"  k1..k100: {cli('exists', *[f'k{i}' for i in range(1,101)])} keys present")
    print(f"  myhash : {cli('hlen', 'myhash')} fields")
    print(f"  mylist : {cli('llen', 'mylist')} elements")
    print(f"  mystream: {cli('xlen', 'mystream')} entries")


def get_usec(cmd_name):
    stats = cli("info", "commandstats")
    key = re.escape("cmdstat_" + cmd_name)
    m = re.search(rf"^{key}:.*usec_per_call=([0-9.]+)", stats, re.MULTILINE)
    return float(m.group(1)) if m else None


def run_bench(cmd_args, extra_flags=None):
    """Run benchmark for a single command; returns (rps, p50_ms)."""
    cli("config", "resetstat")
    flags = extra_flags or []
    result = subprocess.run(
        [BENCH_BIN, "-p", str(PORT),
         "-n", str(N_REQUESTS), "-c", str(N_CLIENTS),
         "-q"] + flags + ["--"] + cmd_args,
        capture_output=True, text=True,
    )
    lines_with_rps = [l for l in result.stdout.splitlines() if "requests per second" in l]
    if not lines_with_rps:
        print(f"  WARNING: no output for {cmd_args}", file=sys.stderr)
        print(f"  stdout: {result.stdout[:200]}", file=sys.stderr)
        return None, None
    last = lines_with_rps[-1]
    rps = re.search(r"([0-9.]+) requests per second", last)
    p50 = re.search(r"p50=([0-9.]+)", last)
    return (
        float(rps.group(1)) if rps else None,
        float(p50.group(1)) if p50 else None,
    )


# ---------------------------------------------------------------------------
# Benchmark cases
# (title, list of inner_cmd lists, extra valkey-benchmark flags)
# ---------------------------------------------------------------------------

VARIANTS = ["test.call", "test.call_argv_passthrough", "test.call_argv"]

# Build inner args for category B (EXISTS 100 keys)
exists_100 = ["exists"] + [f"k{i}" for i in range(1, 101)]

# Each entry: (title, list_of_inner_cmd_lists, extra_bench_flags)
CASES = [
    (
        "Category A — GET mykey",
        [["get", "mykey"]],
        [],
    ),
    (
        "Category B — EXISTS k1..k100 (100 keys)",
        [exists_100],
        [],
    ),
    (
        "Category C — LRANGE mylist 0 -1 (500 elements)",
        [["lrange", "mylist", "0", "-1"]],
        [],
    ),
    (
        "Category C — HGETALL myhash (500 fields)",
        [["hgetall", "myhash"]],
        [],
    ),
    (
        "Category D — XRANGE mystream - + (500 entries)",
        [["xrange", "mystream", "-", "+"]],
        [],
    ),
    (
        "Category E — GET mybigkey (100KB scalar)",
        [["get", "mybigkey"]],
        [],
    ),
    (
        "Category F — RESP3 HGETALL myhash (500 fields, map reply)",
        [["hgetall", "myhash"]],
        ["-3"],
    ),
]


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    build_module()
    start_server()
    try:
        populate()

        all_results = {}  # title -> [(variant, rps, p50, usec), ...]

        for title, inner_cmds, extra_flags in CASES:
            print(f"\n{title}")
            rows = []
            for inner_cmd in inner_cmds:
                for variant in VARIANTS:
                    cmd_args = [variant] + inner_cmd
                    label = f"{variant} {' '.join(inner_cmd[:2])}"
                    print(f"  {label:<55}", end="", flush=True)
                    rps, p50 = run_bench(cmd_args, extra_flags)
                    usec = get_usec(variant)
                    rows.append((variant, rps, p50, usec))
                    rps_s  = f"{rps:>10.0f} rps" if rps  else "         ? rps"
                    p50_s  = f"p50={p50} ms"       if p50  else "p50=? ms"
                    usec_s = f"server={usec} µs"    if usec else "server=? µs"
                    print(f"  {rps_s}  {p50_s:<14}  {usec_s}")
            all_results[title] = rows

    finally:
        stop_server()

    return all_results


def print_summary(all_results):
    print("\n\n" + "=" * 70)
    print("RESULTS SUMMARY")
    print("=" * 70)
    for title, rows in all_results.items():
        print(f"\n### {title}\n")
        print(f"| {'Variant':<30} | {'RPS':>9} | {'p50 (ms)':>8} | {'server µs/call':>14} |")
        print(f"|{'-'*31}|{'-'*11}|{'-'*10}|{'-'*16}|")
        for variant, rps, p50, usec in rows:
            rps_s  = f"{rps:>9.0f}"  if rps  is not None else f"{'?':>9}"
            p50_s  = f"{p50:>8.3f}"  if p50  is not None else f"{'?':>8}"
            usec_s = f"{usec:>14.2f}" if usec is not None else f"{'?':>14}"
            print(f"| {variant:<30} | {rps_s} | {p50_s} | {usec_s} |")


if __name__ == "__main__":
    results = main()
    print_summary(results)
