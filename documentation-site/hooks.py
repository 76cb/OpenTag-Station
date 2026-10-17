"""Display firmware VERSION metadata and refuse unsafe documentation text."""
import html
import json
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def on_config(config):
    metadata = json.loads((ROOT / 'firmware.json').read_text())
    version = metadata['version']
    config['site_name'] = 'OpenTag Station ' + version
    config['copyright'] = 'Firmware ' + version + ' · Release acceptance pending' if '-' in version else 'Firmware ' + version
    config['extra']['firmware'] = metadata
    return config


def on_page_markdown(markdown, **kwargs):
    if ('ca' + 'sy') in markdown.casefold():
        raise ValueError('Documentation privacy check failed')
    return markdown


def page_directory(page):
    """Built directory of a page with use_directory_urls: a/b.md -> a/b/, a/index.md -> a/."""
    stem = page[:-3]
    if stem == 'index' or stem.endswith('/index'):
        return stem[:-5].rstrip('/')
    return stem


def on_post_build(config):
    """Forward the addresses of pages that moved (redirects.json) to their new page."""
    redirects = json.loads((ROOT / 'redirects.json').read_text())
    docs = Path(config['docs_dir'])
    site = Path(config['site_dir'])
    for old, new in redirects.items():
        if old.startswith('_'):
            continue
        if (docs / old).exists():
            raise ValueError(f'Redirect source {old} is still a page')
        if not (docs / new).exists():
            raise ValueError(f'Redirect target {new} does not exist')
        source = site / page_directory(old)
        target = Path(page_directory(new))
        relative = os.path.relpath(target, Path(page_directory(old))).replace(os.sep, '/') + '/'
        url = html.escape(relative)
        canonical = html.escape(config['site_url'].rstrip('/') + '/' + target.as_posix() + '/')
        source.mkdir(parents=True, exist_ok=True)
        (source / 'index.html').write_text(
            '<!doctype html>\n<html lang="en"><head><meta charset="utf-8">'
            f'<title>Page moved</title><link rel="canonical" href="{canonical}">'
            f'<meta http-equiv="refresh" content="0; url={url}">'
            '<meta name="robots" content="noindex"></head>'
            f'<body><p>This page has moved to <a href="{url}">its new address</a>.</p>'
            f'<script>location.replace("{url}" + location.hash)</script></body></html>\n',
            encoding='utf-8')
