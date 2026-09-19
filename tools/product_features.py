"""Read the single production feature default shared with firmware."""
from pathlib import Path
import re


def community_enabled():
    header = Path(__file__).resolve().parents[1] / 'src/config/product_features.hpp'
    match = re.search(r'^#define OPENTAG_ENABLE_COMMUNITY ([01])$', header.read_text(), re.M)
    if not match:
        raise ValueError('Missing explicit production Community feature default')
    return match[1] == '1'
