#!/usr/bin/env python3
"""Fails when too few QML bindings/functions are compiled to C++ by qmlcachegen.

Reads the *.aotstats files qt_add_qml_module writes under <build>/.rcc/qmlcache and compares the share of
entries with codegenSuccessfull=true against a minimum percentage, so new untyped QML (context properties,
untyped JS helpers) cannot silently undo the AOT work. Usage: check_qml_aot.py <build-dir> <min-percent>
"""
import collections
import glob
import json
import os
import re
import sys


def main() -> int:
    build_dir, minimum = sys.argv[1], float(sys.argv[2])
    files = glob.glob(os.path.join(build_dir, ".rcc", "qmlcache", "**", "*.aotstats"), recursive=True)
    if not files:
        print("no .aotstats found: build the zalos target first")
        return 1
    total = compiled = 0
    reasons = collections.Counter()
    for path in files:
        with open(path, encoding="utf-8") as fh:
            data = json.load(fh)
        # Qt <= 6.10: a list of modules, entries with codegenSuccessfull/errorMessage.
        # Qt 6.11+: {"formatRevision": 1, "modules": [...]}, entries with codegenResult (0 = compiled)/message.
        modules = data.get("modules", []) if isinstance(data, dict) else data
        for module in modules:
            for module_file in module.get("moduleFiles", []):
                for entry in module_file.get("entries", []):
                    total += 1
                    if "codegenResult" in entry:
                        ok = entry.get("codegenResult") == 0
                    else:
                        ok = bool(entry.get("codegenSuccessfull"))
                    if ok:
                        compiled += 1
                    else:
                        message = entry.get("message", entry.get("errorMessage", ""))
                        reasons[re.sub(r"(name|property) \"?\w+\"?", r"\1 X", message)[:90]] += 1
    share = 100.0 * compiled / max(1, total)
    print(f"QML AOT: {compiled}/{total} bindings/functions compiled to C++ ({share:.1f}%, minimum {minimum:.0f}%)")
    for message, count in reasons.most_common(5):
        print(f"  {count:5d}  {message}")
    return 0 if share >= minimum else 1


if __name__ == "__main__":
    sys.exit(main())
