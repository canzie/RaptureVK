#!/usr/bin/env python3
"""Move or rename source files and fix every include that points at them.

Knows nothing about any particular layout. It indexes the source tree, resolves each quoted include
to the file it actually names, and repoints the ones whose target moved. An include it cannot resolve
is left alone, so third-party and system headers are never touched.

Include style is preserved: a reference that was relative to the including file stays relative when
that still reaches the target, and becomes root-relative when the move splits the two apart.

  refactor.py refs Engine/src/buffers/Buffers.h        who references this file, and how
  refactor.py mv Engine/src/buffers Engine/src/gpu/buffers
  refactor.py mv Engine/src/logging/Log.h Engine/src/core/utils/Log.h
  refactor.py batch tools/moves.txt

Nothing is written without --apply.
"""

import argparse
import os
import posixpath
import re
import subprocess
import sys
import tempfile

SOURCE_SUFFIXES = (".h", ".hpp", ".inl", ".cpp", ".cc", ".cxx")
INCLUDE_RE = re.compile(r'^(\s*#\s*include\s*)(["<])([^">]+)([">])(.*)$')

DEFAULT_ROOTS = ["Engine/src", "Editor/src"]


def parse_include(line):
    """(prefix, target, angled, suffix) for an include line, or None.

    Angled and quoted are kept apart because only a quoted include searches the includer's own
    directory, and because a bracketed include is far more likely to name a third-party header.
    """
    match = INCLUDE_RE.match(line.rstrip("\r\n"))
    if match is None:
        return None

    prefix, opener, target, closer, rest = match.groups()
    if opener == '"' and closer != '"':
        return None
    if opener == "<" and closer != ">":
        return None

    return prefix + opener, target, opener == "<", closer + rest


class Index:
    """Every source file under the roots, and the includes between them."""

    def __init__(self, repo, roots):
        self.repo = os.path.abspath(repo)
        self.roots = [os.path.join(self.repo, r) for r in roots]
        self.files = []
        for root in self.roots:
            if not os.path.isdir(root):
                continue
            for dirpath, _, filenames in os.walk(root):
                for name in sorted(filenames):
                    if name.endswith(SOURCE_SUFFIXES):
                        self.files.append(os.path.join(dirpath, name))
        self.files.sort()
        self._set = set(self.files)

    def resolve(self, target, from_file, angled=False):
        """The file an include names, or None. Quoted includes search the includer's directory first."""
        if not angled:
            sibling = os.path.normpath(os.path.join(os.path.dirname(from_file), target))
            if sibling in self._set:
                return sibling
        for root in self.roots:
            candidate = os.path.normpath(os.path.join(root, target))
            if candidate in self._set:
                return candidate
        return None

    def root_of(self, path):
        for root in self.roots:
            if path == root or path.startswith(root + os.sep):
                return root
        return None

    def includes(self, path):
        """(line number, text as written, angled, resolved file or None) for each include in a file."""
        out = []
        with open(path, encoding="utf-8", errors="replace") as handle:
            for number, line in enumerate(handle, 1):
                parsed = parse_include(line)
                if parsed is not None:
                    _, target, angled, _ = parsed
                    out.append((number, target, angled, self.resolve(target, path, angled)))
        return out

    def references(self, target):
        """Every (file, line, text as written) naming this file."""
        target = os.path.abspath(target)
        found = []
        for path in self.files:
            for number, written, _, resolved in self.includes(path):
                if resolved == target:
                    found.append((path, number, written))
        return found

    def find_by_suffix(self, target):
        """Files whose root-relative path ends with target, then by basename. Used to repair a stale include."""
        wanted = "/" + target.replace(os.sep, "/")
        matches = [p for p in self.files if p.replace(os.sep, "/").endswith(wanted)]
        if matches:
            return matches
        base = posixpath.basename(target)
        return [p for p in self.files if os.path.basename(p) == base]


def expand_move(src, dst):
    """A move of a file or a directory, as a flat file to file mapping."""
    src = os.path.abspath(src)
    dst = os.path.abspath(dst)

    if os.path.isfile(src):
        if os.path.isdir(dst):
            dst = os.path.join(dst, os.path.basename(src))
        return {src: dst}

    if os.path.isdir(src):
        mapping = {}
        for dirpath, _, filenames in os.walk(src):
            for name in filenames:
                full = os.path.join(dirpath, name)
                mapping[full] = os.path.join(dst, os.path.relpath(full, src))
        return mapping

    sys.exit(f"no such file or directory: {src}")


def include_text(target, including_file, index, was_relative):
    """How an include of target should read from including_file, keeping the original style."""
    if was_relative:
        relative = os.path.relpath(target, os.path.dirname(including_file))
        if not relative.startswith(".."):
            return relative.replace(os.sep, "/")

    root = index.root_of(target)
    if root is None:
        root = index.root_of(including_file)
    return os.path.relpath(target, root).replace(os.sep, "/")


def rewrite_file(path, mapping, index):
    """Repoint the includes in one file. Returns the new text, or None if nothing changed."""
    with open(path, encoding="utf-8") as handle:
        lines = handle.readlines()

    new_path = mapping.get(path, path)
    changed = False

    for i, line in enumerate(lines):
        parsed = parse_include(line)
        if parsed is None:
            continue

        prefix, target, angled, suffix = parsed
        resolved = index.resolve(target, path, angled)
        if resolved is None:
            continue

        new_target = mapping.get(resolved, resolved)
        if new_target == resolved and new_path == path:
            continue

        sibling = os.path.normpath(os.path.join(os.path.dirname(path), target))
        was_relative = not angled and sibling == resolved

        replacement = include_text(new_target, new_path, index, was_relative)
        if replacement == target:
            continue

        ending = "\n" if line.endswith("\n") else ""
        lines[i] = prefix + replacement + suffix + ending
        changed = True

    return "".join(lines) if changed else None


def prune_empty_dirs(root):
    # listdir rather than the walk's own lists, which are captured before the children are removed
    for dirpath, _, _ in os.walk(root, topdown=False):
        if dirpath != root and not os.listdir(dirpath):
            os.rmdir(dirpath)


def do_move(repo, index, moves, apply):
    """moves is a list of (src, dst). Applied in order, re-indexing after each."""
    total_files = 0
    total_refs = 0

    for src, dst in moves:
        if not os.path.exists(src):
            if apply:
                sys.exit(f"no such file or directory: {src}")
            # a dry run moves nothing, so anything an earlier line would have created is not there yet
            print(f"  {os.path.relpath(src, repo)} -> {os.path.relpath(dst, repo)}"
                  f"   not present yet, produced by an earlier move")
            continue

        mapping = expand_move(src, dst)
        if not mapping:
            print(f"  {os.path.relpath(src, repo)} is empty, skipped")
            continue

        edits = {}
        for path in index.files:
            new_text = rewrite_file(path, mapping, index)
            if new_text is not None:
                edits[path] = new_text

        refs = sum(1 for path in mapping for _ in index.references(path))
        total_files += len(edits)
        total_refs += refs
        print(f"  {os.path.relpath(src, repo)} -> {os.path.relpath(dst, repo)}"
              f"   {len(mapping)} files, {refs} references in {len(edits)} files")

        if not apply:
            continue

        for old, new in sorted(mapping.items()):
            os.makedirs(os.path.dirname(new), exist_ok=True)
            subprocess.run(["git", "mv", old, new], cwd=repo, check=True)

        for path, text in edits.items():
            destination = mapping.get(path, path)
            with open(destination, "w", encoding="utf-8") as handle:
                handle.write(text)

        for root in index.roots:
            if os.path.isdir(root):
                prune_empty_dirs(root)
        index = Index(repo, [os.path.relpath(r, repo) for r in index.roots])

    print(f"\n{total_refs} references across {total_files} files"
          f"{'' if apply else ' would be rewritten, nothing written. pass --apply'}")


def cmd_refs(repo, index, args):
    target = os.path.abspath(args.path)
    found = index.references(target)
    print(f"{os.path.relpath(target, repo)} is referenced in {len(found)} places")
    for path, number, written in found:
        print(f"  {os.path.relpath(path, repo)}:{number}   #include \"{written}\"")


def cmd_fix(repo, index, args):
    """Repoint includes that resolve to nothing at a file of the same name, where there is exactly one."""
    proposals = []
    ambiguous = []
    unknown = []

    for path in index.files:
        for number, target, angled, resolved in index.includes(path):
            if resolved is not None:
                continue
            candidates = index.find_by_suffix(target)
            if len(candidates) == 1:
                proposals.append((path, number, target, angled, candidates[0]))
            elif candidates:
                ambiguous.append((path, number, target, candidates))
            else:
                unknown.append((path, number, target))

    print(f"{len(proposals)} repairable, {len(ambiguous)} ambiguous, {len(unknown)} unknown\n")

    edits = {}
    for path, number, target, angled, found in proposals:
        replacement = include_text(found, path, index, was_relative=False)
        print(f"  {os.path.relpath(path, repo)}:{number}   {target}  ->  {replacement}")
        edits.setdefault(path, []).append((number, target, replacement))

    if ambiguous:
        print(f"\nambiguous, left alone:")
        for path, number, target, candidates in ambiguous:
            names = ", ".join(os.path.relpath(c, repo) for c in candidates[:3])
            print(f"  {os.path.relpath(path, repo)}:{number}   {target}   matches {len(candidates)}: {names}")

    if not args.apply:
        print("\nnothing written. pass --apply")
        return

    for path, changes in edits.items():
        with open(path, encoding="utf-8") as handle:
            lines = handle.readlines()
        for number, target, replacement in changes:
            parsed = parse_include(lines[number - 1])
            if parsed is None:
                continue
            prefix, written, _, suffix = parsed
            if written != target:
                continue
            ending = "\n" if lines[number - 1].endswith("\n") else ""
            lines[number - 1] = prefix + replacement + suffix + ending
        with open(path, "w", encoding="utf-8") as handle:
            handle.writelines(lines)

    print(f"\n{sum(len(c) for c in edits.values())} includes repaired in {len(edits)} files")


def cmd_mv(repo, index, args):
    do_move(repo, index, [(args.src, args.dst)], args.apply)


def cmd_batch(repo, index, args):
    moves = []
    with open(args.file, encoding="utf-8") as handle:
        for raw in handle:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) != 2:
                sys.exit(f"expected two paths per line: {raw.strip()}")
            moves.append((os.path.join(repo, parts[0]), os.path.join(repo, parts[1])))
    print(f"{len(moves)} moves")
    do_move(repo, index, moves, args.apply)


def selftest():
    with tempfile.TemporaryDirectory() as tmp:
        def write(rel, text):
            full = os.path.join(tmp, rel)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, "w") as handle:
                handle.write(text)

        write("Engine/src/buffers/Buffers.h", "#pragma once\n")
        write("Engine/src/buffers/BufferPool.h", '#include "Buffers.h"\n')
        write("Engine/src/buffers/descriptors/DescriptorSet.h", '#include "buffers/Buffers.h"\n')
        write("Engine/src/renderer/Renderer.h",
              '#include "buffers/Buffers.h"\n#include <vector>\n#include "yyjson.h"\n')
        write("Editor/src/Panel.h", '#include "buffers/Buffers.h"\n')

        subprocess.run(["git", "init", "-q"], cwd=tmp, check=True)
        subprocess.run(["git", "add", "-A"], cwd=tmp, check=True)
        subprocess.run(["git", "-c", "user.email=t@t", "-c", "user.name=t", "commit", "-qm", "x"],
                       cwd=tmp, check=True)

        index = Index(tmp, DEFAULT_ROOTS)

        # the index finds every reference regardless of how it was written
        refs = index.references(os.path.join(tmp, "Engine/src/buffers/Buffers.h"))
        assert len(refs) == 4, refs

        # unknown includes resolve to nothing rather than guessing
        assert index.resolve("yyjson.h", os.path.join(tmp, "Engine/src/renderer/Renderer.h")) is None

        do_move(tmp, index, [(os.path.join(tmp, "Engine/src/buffers"),
                              os.path.join(tmp, "Engine/src/gpu/buffers"))], apply=True)

        def read(rel):
            with open(os.path.join(tmp, rel)) as handle:
                return handle.read()

        # the whole subtree moved, relative structure intact
        assert os.path.isfile(os.path.join(tmp, "Engine/src/gpu/buffers/Buffers.h"))
        assert os.path.isfile(os.path.join(tmp, "Engine/src/gpu/buffers/descriptors/DescriptorSet.h"))
        assert not os.path.exists(os.path.join(tmp, "Engine/src/buffers"))
        # a sibling reference stayed relative, because both files moved together
        assert read("Engine/src/gpu/buffers/BufferPool.h") == '#include "Buffers.h"\n'
        # root-relative references were repointed, in the engine and in the editor
        assert read("Engine/src/gpu/buffers/descriptors/DescriptorSet.h") == '#include "gpu/buffers/Buffers.h"\n'
        assert read("Editor/src/Panel.h") == '#include "gpu/buffers/Buffers.h"\n'
        # untouched includes stayed untouched
        assert read("Engine/src/renderer/Renderer.h") == \
            '#include "gpu/buffers/Buffers.h"\n#include <vector>\n#include "yyjson.h"\n'

        # a rename is a move whose basename differs
        index = Index(tmp, DEFAULT_ROOTS)
        do_move(tmp, index, [(os.path.join(tmp, "Engine/src/gpu/buffers/Buffers.h"),
                              os.path.join(tmp, "Engine/src/gpu/buffers/GpuBuffers.h"))], apply=True)
        assert read("Engine/src/gpu/buffers/BufferPool.h") == '#include "GpuBuffers.h"\n'
        assert read("Editor/src/Panel.h") == '#include "gpu/buffers/GpuBuffers.h"\n'

        # splitting two siblings apart turns the relative reference into a rooted one
        index = Index(tmp, DEFAULT_ROOTS)
        do_move(tmp, index, [(os.path.join(tmp, "Engine/src/gpu/buffers/GpuBuffers.h"),
                              os.path.join(tmp, "Engine/src/core/GpuBuffers.h"))], apply=True)
        assert read("Engine/src/gpu/buffers/BufferPool.h") == '#include "core/GpuBuffers.h"\n'

    print("all tests passed")


def main():
    # shared so the flags read the same before or after the subcommand
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--repo", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    common.add_argument("--root", action="append", dest="roots", help="source root, repeatable")
    common.add_argument("--apply", action="store_true", help="write the changes")
    common.add_argument("--selftest", action="store_true", help="run the tests and exit")

    parser = argparse.ArgumentParser(description=__doc__, parents=[common],
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command")

    refs = sub.add_parser("refs", parents=[common], help="list every reference to a file")
    refs.add_argument("path")
    refs.set_defaults(func=cmd_refs)

    mv = sub.add_parser("mv", parents=[common], help="move or rename a file or directory")
    mv.add_argument("src")
    mv.add_argument("dst")
    mv.set_defaults(func=cmd_mv)

    batch = sub.add_parser("batch", parents=[common], help="apply a file of 'src dst' lines in order")
    batch.add_argument("file")
    batch.set_defaults(func=cmd_batch)

    fix = sub.add_parser("fix", parents=[common], help="repoint includes that no longer resolve")
    fix.set_defaults(func=cmd_fix)

    args = parser.parse_args()

    if args.selftest:
        selftest()
        return

    if args.command is None:
        parser.print_help()
        return

    index = Index(args.repo, args.roots or DEFAULT_ROOTS)
    args.func(args.repo, index, args)


if __name__ == "__main__":
    main()
