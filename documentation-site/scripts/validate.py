"""Check source/output privacy, every local link/anchor, and image integrity."""
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlparse, unquote
import argparse
import hashlib
import json
import struct
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
TEXT = {'.md', '.html', '.json', '.txt', '.svg', '.js', '.css', '.py', '.yml'}


class Page(HTMLParser):
    def __init__(self, text):
        super().__init__()
        self.ids, self.links = set(), []
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if 'id' in attrs:
            self.ids.add(attrs['id'])
        for attr in ['href', 'src']:
            if attrs.get(attr) and tag != 'link':
                self.links.append(attrs[attr])


def check(root):
    pages = {}
    for path in root.rglob('*'):
        if not path.is_file() or any(p in {'.git', '.venv', '__pycache__'} for p in path.parts):
            continue
        if path.suffix in TEXT:
            text = path.read_text(encoding='utf-8')
            if ('ca' + 'sy') in text.casefold():
                raise ValueError(f'Privacy check failed: {path}')
            if path.suffix == '.html':
                pages[path.resolve()] = Page(text)
            if path.suffix == '.svg':
                ET.parse(path)
        elif path.suffix == '.png':
            data = path.read_bytes()
            assert data[:8] == b'\x89PNG\r\n\x1a\n' and len(data) > 32, path
            assert all(struct.unpack('>II', data[16:24])), path
    failures = []
    for path, page in pages.items():
        for link in page.links:
            url = urlparse(link)
            if url.scheme or url.netloc:
                continue
            target = unquote(url.path)
            if target.startswith('/OpenTag-Station-Docs/'):
                destination = root / target.removeprefix('/OpenTag-Station-Docs/')
            elif target.startswith('/'):
                destination = root / target.lstrip('/')
            else:
                destination = path.parent / target if target else path
            if destination.is_dir():
                destination /= 'index.html'
            destination = destination.resolve()
            if not destination.exists():
                failures.append(f'{path}: missing {link}')
            elif url.fragment and destination in pages and unquote(url.fragment) not in pages[destination].ids:
                failures.append(f'{path}: missing anchor {link}')
    if failures:
        raise ValueError('\n'.join(failures))
    print(f'Documentation privacy, local links, anchors and images passed: {len(pages)} HTML pages')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, nargs='?', default=ROOT / 'site')
    args = parser.parse_args()
    check(args.directory)
