#!/usr/bin/env python3
"""review-court の手順0: LLMを使わない静的解析。

使い方: python3 .claude/review/static-check.py <パス>...   (ファイルまたはディレクトリ)
build/compile_commands.json の各 .cpp のコンパイルコマンドで、
clang++ -fsyntax-only に警告を足して実行し、リポジトリ内のファイルの警告だけを出す。
ビルドは更新しない(出力は捨てる)。警告が無ければ「警告なし」と出す。
"""
import json, os, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WARN = ["-Wall", "-Wextra", "-Wunused", "-Wshadow", "-Wunreachable-code", "-Wunused-macros",
        "-Wunused-member-function", "-Wunused-template", "-Wdeprecated", "-Wnull-dereference",
        "-Wimplicit-fallthrough", "-Wconversion-null"]

def targets(paths, known):
    out = []
    for p in paths:
        p = os.path.abspath(p)
        if os.path.isdir(p):
            out += [f for f in known if f.startswith(p + os.sep)]
        elif p in known:
            out.append(p)
        else:  # ヘッダなど: 同名の .cpp か、そのディレクトリの .cpp
            base = os.path.splitext(p)[0].split(os.sep)[-1]
            out += [f for f in known if os.path.splitext(os.path.basename(f))[0] == base]
    return sorted(set(out))

def check(entry):
    args = shlex.split(entry["command"])
    cmd = []
    skip = False
    for a in args:
        if skip:
            skip = False
            continue
        if a == "-o":
            skip = True
            continue
        if a == "-c":
            continue
        cmd.append(a)
    cmd = [c for c in cmd if c != entry["file"]] + WARN + ["-fsyntax-only", "-fno-color-diagnostics", entry["file"]]
    r = subprocess.run(cmd, cwd=entry["directory"], capture_output=True, text=True)
    lines = []
    for l in r.stderr.splitlines():
        if ": warning:" in l or ": error:" in l:
            path = l.split(":", 1)[0]
            if path.startswith(ROOT) and not path.startswith(os.path.join(ROOT, "build")):
                lines.append(l.replace(ROOT + os.sep, ""))
    return lines

def main():
    cc = json.load(open(os.path.join(ROOT, "build", "compile_commands.json")))
    known = {e["file"]: e for e in cc}
    files = targets(sys.argv[1:] or [os.path.join(ROOT, "src")], known)
    if not files:
        print("対象の .cpp が compile_commands.json に無い")
        return
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        results = list(ex.map(lambda f: check(known[f]), files))
    seen, out = set(), []
    for lines in results:
        for l in lines:
            if l not in seen:
                seen.add(l)
                out.append(l)
    print(f"対象 {len(files)} ファイル")
    print("\n".join(out) if out else "警告なし")

main()
