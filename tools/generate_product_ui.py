"""Embed the dependency-free presentation sources; --check detects stale output."""
from pathlib import Path
import argparse
import re

root=Path(__file__).resolve().parents[1]
p=root/'src/web/web_assets.cpp'
s=p.read_text(encoding='utf-8')
css=(root/'src/web/ui/product.css').read_text(encoding='utf-8')
js=(root/'src/web/ui/product.js').read_text(encoding='utf-8')
s=re.sub(r'const char application_css\[\] = R"CSS\(.*?\)CSS";',lambda m:'const char application_css[] = R"CSS('+css+')CSS";',s,flags=re.S)
begin='// PRODUCT PRESENTATION BEGIN\n';end='// PRODUCT PRESENTATION END\n'
if begin in s:
    a=s.index(begin);b=s.index(end,a)+len(end);s=s[:a]+begin+js+end+s[b:]
else:
    s=s.replace('async function start() {',begin+js+end+'\nasync function start() {')
parser=argparse.ArgumentParser();parser.add_argument('--check',action='store_true');args=parser.parse_args()
if args.check:
    if p.read_text(encoding='utf-8')!=s:raise SystemExit('Run python tools/generate_product_ui.py')
else:p.write_text(s,encoding='utf-8')
