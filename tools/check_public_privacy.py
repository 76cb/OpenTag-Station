"""Fail closed before public fixture rendering and artifact publication."""
from pathlib import Path
import argparse

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN = "ca" + "sy"
TEXT = {'.md', '.html', '.json', '.txt', '.svg', '.js', '.cjs', '.mjs', '.py', '.yml', '.yaml', '.css'}


def assert_public_text(text: str, context: str = 'public fixture') -> None:
    if FORBIDDEN in text.casefold():
        raise ValueError(f'Privacy check failed: prohibited personal identifier in {context}')


def scan(paths) -> int:
    count = 0
    for root in paths:
        if not root.exists():
            raise ValueError(f'Missing privacy scan input: {root}')
        for path in ([root] if root.is_file() else root.rglob('*')):
            if path.is_file() and path.suffix.lower() in TEXT:
                if any(p in {'.git', '__pycache__', '.venv', 'node_modules'} for p in path.parts):
                    continue
                assert_public_text(path.read_text(encoding='utf-8'), str(path))
                count += 1
    return count


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paths', nargs='*', type=Path)
    args = parser.parse_args()
    paths = args.paths or [ROOT / p for p in ('README.md', 'docs', 'tools', 'web-flasher', 'documentation-site')]
    print(f'Public privacy check passed: {scan(paths)} text inputs')
