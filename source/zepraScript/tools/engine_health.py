#!/usr/bin/env python3
"""
ZepraScript Engine Health Monitor
Scans the codebase and reports live subsystem metrics.

Usage:
    python3 engine_health.py                  # One-shot report
    python3 engine_health.py --watch          # Live updates every 5s
    python3 engine_health.py --json           # JSON output
"""

import os
import sys
import re
import json
import time
import argparse
from pathlib import Path
from collections import defaultdict

ENGINE_ROOT = Path(__file__).resolve().parent.parent

SUBSYSTEMS = {
    "frontend":    {"path": "frontend",    "target": 80, "desc": "Parser/Lexer/AST"},
    "bytecode":    {"path": "bytecode",    "target": 70, "desc": "Bytecode compiler"},
    "runtime":     {"path": "runtime",     "target": 70, "desc": "VM, objects, async"},
    "builtins":    {"path": "builtins",    "target": 85, "desc": "Array, String, Math..."},
    "jit":         {"path": "jit",         "target": 40, "desc": "JIT compiler tiers"},
    "heap":        {"path": "heap",        "target": 55, "desc": "GC / Heap"},
    "memory":      {"path": "memory",      "target": 15, "desc": "Pool allocators"},
    "wasm":        {"path": "wasm",        "target": 60, "desc": "WebAssembly"},
    "browser":     {"path": "browser",     "target": 50, "desc": "DOM/Fetch/Events"},
    "api":         {"path": "api",         "target": 65, "desc": "Embedding API"},
    "zir":         {"path": "zir",         "target": 35, "desc": "Backend IR"},
    "zopt":        {"path": "zopt",        "target": 45, "desc": "DFG optimizer"},
    "interpreter": {"path": "interpreter", "target": 10, "desc": "Bytecode interpreter"},
    "exception":   {"path": "exception",   "target": 50, "desc": "Exception handling"},
    "host":        {"path": "host",        "target": 20, "desc": "C++/JS bridge"},
    "debugger":    {"path": "debugger",    "target": 50, "desc": "Debug API"},
}

STUB_PATTERNS = [
    r'\bTODO\b', r'\bFIXME\b', r'\bstub\b', r'\bFor now\b',
    r'\bplaceholder\b', r'\bnot implemented\b', r'\bHACK\b',
]

SECURITY_KEYWORDS = ['sandbox', 'CSP', 'sanitize', 'SafeZone', 'isolation', 'XSS']
CRASH_KEYWORDS = ['crash', 'recovery', 'watchdog', 'signal', 'SIGSEGV', 'minidump']


def count_lines(directory):
    total = 0
    file_count = 0
    if not directory.exists():
        return 0, 0
    for ext in ('*.cpp', '*.hpp', '*.h'):
        for f in directory.rglob(ext):
            file_count += 1
            with open(f, 'r', errors='ignore') as fh:
                total += sum(1 for _ in fh)
    return total, file_count


def count_stubs(directory):
    stubs = 0
    if not directory.exists():
        return 0
    for ext in ('*.cpp', '*.hpp', '*.h'):
        for f in directory.rglob(ext):
            with open(f, 'r', errors='ignore') as fh:
                for line in fh:
                    for pat in STUB_PATTERNS:
                        if re.search(pat, line, re.IGNORECASE):
                            stubs += 1
                            break
    return stubs


def count_keyword_files(root, keywords):
    hits = set()
    for ext in ('*.cpp', '*.hpp', '*.h'):
        for f in root.rglob(ext):
            with open(f, 'r', errors='ignore') as fh:
                content = fh.read()
                for kw in keywords:
                    if kw.lower() in content.lower():
                        hits.add(str(f))
                        break
    return len(hits)


def bar(pct, width=20):
    filled = int(pct / 100 * width)
    return '█' * filled + '░' * (width - filled)


def collect_report():
    report = {
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "subsystems": {},
        "totals": {"lines": 0, "files": 0, "stubs": 0},
        "security_files": 0,
        "crash_files": 0,
    }

    for name, info in SUBSYSTEMS.items():
        dirpath = ENGINE_ROOT / info["path"]
        lines, files = count_lines(dirpath)
        stubs = count_stubs(dirpath)
        report["subsystems"][name] = {
            "lines": lines,
            "files": files,
            "stubs": stubs,
            "maturity_pct": info["target"],
            "description": info["desc"],
        }
        report["totals"]["lines"] += lines
        report["totals"]["files"] += files
        report["totals"]["stubs"] += stubs

    report["security_files"] = count_keyword_files(ENGINE_ROOT, SECURITY_KEYWORDS)
    report["crash_files"] = count_keyword_files(ENGINE_ROOT, CRASH_KEYWORDS)

    return report


def print_report(report):
    print("\033[2J\033[H")  # clear screen
    print("╔══════════════════════════════════════════════════════════════════╗")
    print("║          ZepraScript Engine Health Monitor                      ║")
    print(f"║          {report['timestamp']}                               ║")
    print("╠══════════════════════════════════════════════════════════════════╣")
    print(f"║  Total Lines: {report['totals']['lines']:>7,}  │  "
          f"Files: {report['totals']['files']:>4}  │  "
          f"Stubs: {report['totals']['stubs']:>3}           ║")
    print("╠══════════════════════════════════════════════════════════════════╣")
    print("║  Subsystem          Lines   Stubs  Maturity                    ║")
    print("║  ─────────────────  ──────  ─────  ────────────────────────    ║")

    for name, data in sorted(report["subsystems"].items(),
                              key=lambda x: x[1]["lines"], reverse=True):
        pct = data["maturity_pct"]
        b = bar(pct, 16)
        print(f"║  {name:<18} {data['lines']:>6,}  {data['stubs']:>5}  "
              f"{b} {pct:>3}%  ║")

    print("╠══════════════════════════════════════════════════════════════════╣")
    print(f"║  Security coverage: {report['security_files']:>3} files  │  "
          f"Crash recovery: {report['crash_files']:>3} files         ║")
    print("╠══════════════════════════════════════════════════════════════════╣")

    # Overall health score (weighted average)
    total_weight = 0
    weighted_sum = 0
    for data in report["subsystems"].values():
        weight = max(data["lines"], 100)  # minimum weight
        weighted_sum += data["maturity_pct"] * weight
        total_weight += weight
    overall = weighted_sum / total_weight if total_weight else 0

    ob = bar(overall, 30)
    print(f"║  Overall Health: {ob} {overall:.0f}%       ║")
    print("╚══════════════════════════════════════════════════════════════════╝")


def main():
    parser = argparse.ArgumentParser(description="ZepraScript Engine Health Monitor")
    parser.add_argument("--watch", action="store_true", help="Live updates every 5s")
    parser.add_argument("--json", action="store_true", help="Output as JSON")
    parser.add_argument("--interval", type=int, default=5, help="Watch interval (s)")
    args = parser.parse_args()

    if args.json:
        print(json.dumps(collect_report(), indent=2))
        return

    if args.watch:
        try:
            while True:
                report = collect_report()
                print_report(report)
                print(f"\n  Refreshing every {args.interval}s... (Ctrl+C to stop)")
                time.sleep(args.interval)
        except KeyboardInterrupt:
            print("\nMonitor stopped.")
    else:
        report = collect_report()
        print_report(report)


if __name__ == "__main__":
    main()
