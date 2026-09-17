"""Display firmware VERSION metadata and refuse unsafe documentation text."""
import json
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
