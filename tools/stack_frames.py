"""Shared compiler-frame parsing for production stack-budget checks."""
from pathlib import Path
from analyze_stack_usage import parse_line


def frame_entries(build_dir: Path) -> list[tuple[Path, int, str]]:
    entries = []
    for path in sorted(build_dir.rglob('*.su')):
        for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
            parsed = parse_line(line)
            if parsed is not None:
                size, function, _ = parsed
                entries.append((path, size, function))
    return entries


def require_frame(entries: list[tuple[Path, int, str]], source_suffix: str,
                  function_fragment: str) -> int:
    matches = [size for path, size, function in entries
               if path.as_posix().endswith(source_suffix)
               and function_fragment in function and '::<lambda' not in function]
    if len(matches) != 1:
        raise ValueError(f'expected one {function_fragment!r} frame in {source_suffix}, found {len(matches)}')
    return matches[0]
