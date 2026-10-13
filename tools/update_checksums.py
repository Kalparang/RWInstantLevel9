"""Update or verify the public repository's file manifest (Python standard library).

Only the listed source/documentation roots and top-level release files are used.
Builds, saves, logs, caches and Git metadata are never added to the manifest.
This is an integrity manifest, not an archive builder or a secrets scanner.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
TOP_LEVEL = ('.gitattributes', '.gitignore', 'CMakeLists.txt', 'README.md', 'THIRD_PARTY_NOTICES.txt', 'winhttp.dll')
SOURCE_ROOTS = ('src', 'tools', 'tests', 'docs', 'third_party/minhook')
SOURCE_SUFFIXES = {'.c', '.h', '.cpp', '.asm', '.inc', '.def', '.json', '.py', '.ps1', '.md', '.txt'}


def public_files() -> list[Path]:
    paths = [ROOT / name for name in TOP_LEVEL]
    for name in SOURCE_ROOTS:
        for path in (ROOT / name).rglob('*'):
            if '__pycache__' in path.parts:
                continue
            if path.is_file():
                if path.is_symlink() or path.suffix not in SOURCE_SUFFIXES:
                    raise ValueError(f'Unexpected source file: {path.relative_to(ROOT)}')
                paths.append(path)
    for path in paths:
        if path.is_symlink() or not path.is_file():
            raise ValueError(f'Missing or linked release file: {path.relative_to(ROOT)}')
    return sorted(paths, key=lambda path: path.relative_to(ROOT).as_posix())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Verify without changing SHA256SUMS.txt')
    args = parser.parse_args()
    try:
        paths = public_files()
        content = ''.join(f'{hashlib.sha256(path.read_bytes()).hexdigest().upper()}  '
                          f'{path.relative_to(ROOT).as_posix()}\n' for path in paths)
        manifest = ROOT / 'SHA256SUMS.txt'
        if args.check:
            if manifest.read_text(encoding='ascii') != content:
                raise ValueError('SHA256SUMS.txt does not match the current public files')
        else:
            manifest.write_text(content, encoding='ascii', newline='\n')
        print(f'{"Verified" if args.check else "Recorded"} {len(paths)} public files.')
        return 0
    except (OSError, ValueError) as error:
        print(f'Error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
