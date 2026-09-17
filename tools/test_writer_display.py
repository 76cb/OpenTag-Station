#!/usr/bin/env python3
"""Real Chromium CSS/layout tests against the production writer asset.

The fixture has a browser-only fake Spoolman host: no network or NFC operations.
Use --output to create an interactive review fixture instead of running Chromium.
"""
import argparse
import functools
import http.server
import re
import os
import pathlib
import shutil
import subprocess
import tempfile
import threading

from web_asset_compression import browser_assets, writer_assets

HOST = r"""
window.__OPENTAG_TEST__=true;
const spool={id:28,initial_weight:1000,remaining_weight:1000,used_weight:0,spool_weight:130,archived:false,
filament:{id:22,name:'Sunlu PLA+ 2.0 Black',material:'PLA+',weight:777.12,density:1.24,diameter:1.75,color_hex:'000000',vendor:{id:5,name:'Sunlu'}}};
const preview={phase:'preview',mode:'rewrite',spool_id:28,previous_spool_id:0,uid:'E00401086627D8D4',generation:'3',current_checksum:'9E639911',target_checksum:'B57D9186',changed_blocks:[2,3,7],instance_uuid:'fea84b36-1234-4234-9234-123456789012',current:{},proposed:{brand_name:'Sunlu',material_name:'Sunlu PLA+ 2.0 Black',material_abbreviation:'PLA+',nominal_netto_full_weight:1000,actual_netto_full_weight:1000,empty_container_weight:130,density:1.24,filament_diameter:1.75,consumed_weight:0,primary_color:[0,0,0,255]},warnings:['Writing is not atomic. Keep tag and power in place.','missing recommended GTIN','missing recommended manufactured date','missing recommended min_print_temperature']};
let result={phase:'idle'};
const id=n=>document.getElementById(n);
window.OpenTagWriterHost={byId:id,asObject:v=>v&&typeof v==='object'?v:{},asArray:v=>Array.isArray(v)?v:[],first:(...v)=>v.find(x=>x!==null&&x!==undefined),setText:(n,v)=>id(n).textContent=v,setValue:(n,v)=>id(n).value=v,valueOf:n=>id(n).value,showToast:m=>id('fixture-status').textContent=m,PRIORITY:{CONTROL:0},state:{},api:async()=>structuredClone(result),submitMutation:async(path,{body})=>{
if(body.action==='catalog')result={phase:'catalog',entity:'spool',items:[{...spool,id:27,remaining_weight:0,used_weight:1000},spool],offset:0,next_offset:2,has_more:false};
if(body.action==='preview')result={...preview,spool:structuredClone(spool)};
if(body.action==='update_filament'){Object.assign(spool.filament,body.changes);result={phase:'updated',spool:structuredClone(spool),filament:structuredClone(spool.filament),message:'Saved and verified in Spoolman. Generate a new tag preview.'};}
if(body.action==='update_spool'){Object.assign(spool,body.changes);spool.remaining_weight=Math.max(0,spool.initial_weight-spool.used_weight);result={phase:'updated',spool:structuredClone(spool),message:'Saved and verified in Spoolman. Generate a new tag preview.'};}
if(body.action==='write')result={...preview,phase:'complete',message:'Tag and Spoolman association verified (fixture only).'};
}};
"""
CHECKS = r"""
window.OpenTagWriter.bind();window.OpenTagWriter.bindWriter();
const T=window.OpenTagWriter;
async function checks(){let count=0;const check=(v,m)=>{if(!v)throw Error(m);++count;};
await T.writerSearch(false);const rows=id('writer-results').children;
check(id('writer-preview').disabled,'preview must start disabled');rows[1].click();
check(rows[1].getAttribute('aria-pressed')==='true','selected aria state');
check(rows[1].textContent.includes('✓'),'visible checkmark');
check(getComputedStyle(rows[1]).backgroundColor!==getComputedStyle(rows[0]).backgroundColor,'selected background');
check(getComputedStyle(rows[1].querySelector('.writer-swatch')).backgroundColor==='rgb(0, 0, 0)','CSP-safe color swatch');
check(id('writer-selection').textContent.includes('#28'),'selected details');
check(!id('writer-preview').disabled,'selected preview enabled');
T.openEditor('filament');check(id('writer-editor-warning').textContent.includes('every Spoolman spool'),'shared scope');
const weight=id('writer-editor-fields').querySelector('[name="weight"]');check(weight.value==='777.12','original nominal weight');weight.value='1000';await T.saveEditor();
check(T.writerState.material.weight===1000,'verified nominal weight');
check(getComputedStyle(id('writer-editor')).display==='none','editor closes after save');
await T.writerCommand({action:'preview',spool_id:28});
check(id('writer-diff').textContent.includes('1000 g'),'human readable diff');
check(!id('writer-advanced').open,'advanced collapsed');check(!id('writer-notices').open,'optional notices collapsed');
for(const phase of ['idle','failed','preview','import_preview','association_pending','complete']){
T.renderWriter({...preview,phase});for(const [name,active] of [['confirm','preview'],['import','import_preview'],['retry','association_pending']]){
const node=id('writer-'+name);node.hidden=false;check((getComputedStyle(node).display!=='none')===(phase===active),'CSS visibility '+name+' '+phase);
}}
T.renderWriter(preview);
check(getComputedStyle(document.querySelector('.writer-grid')).gridTemplateColumns.split(' ').length===(innerWidth<=760?1:2),'responsive columns');
check(document.documentElement.scrollWidth<=innerWidth,'no horizontal page overflow');
for(const name of ['previous','next','preview','confirm','edit-spool','edit-filament'])check(id('writer-'+name).isConnected,'responsive control '+name);
id('fixture-status').textContent='PASS: '+count+' real-browser assertions';id('fixture-status').dataset.result='passed';
}
checks().catch(e=>{id('fixture-status').textContent='FAIL: '+e.message;id('fixture-status').dataset.result='failed';});
"""


def fixture(page):
    css, _ = browser_assets()
    writer = b"".join(writer_assets()).decode("utf-8")
    files = {'writer-fixture.css': css.decode("utf-8") + 'body{padding:16px}main{max-width:1180px;margin:auto}',
             'writer-host.js': HOST, 'writer-production.js': writer, 'writer-checks.js': CHECKS}
    for name, content in files.items():
        (page.parent / name).write_text(content, encoding='utf-8')
    server = pathlib.Path(__file__).resolve().parents[1] / 'src/web/local_web_server.cpp'
    policy = re.search(r'\{"Content-Security-Policy",(.*?)\}', server.read_text(), re.S)[1]
    policy = ''.join(re.findall(r'"([^"]*)"', policy))
    return ('<!doctype html><html><head><meta charset="utf-8">'
            '<meta http-equiv="Content-Security-Policy" content="' + policy + '">'
            '<meta name="viewport" content="width=device-width,initial-scale=1">'
            '<title>OpenTag writer UX fixture</title><link rel="stylesheet" href="writer-fixture.css"></head><body>'
            '<main><p id="fixture-status">Browser-only fixture · no station connection</p>'
            '<button id="writer-open" class="button">Open Writer</button><div id="writer-panel"></div></main>'
            '<script src="writer-host.js"></script><script src="writer-production.js"></script>'
            '<script src="writer-checks.js"></script></body></html>')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    if args.output:
        args.output.write_text(fixture(args.output), encoding='utf-8')
        return
    chrome = os.environ.get('CHROME_BIN') or shutil.which('google-chrome') or shutil.which('chromium')
    if not chrome:
        raise SystemExit('Chromium required; set CHROME_BIN or use --output for interactive review')
    with tempfile.TemporaryDirectory(prefix='opentag-writer-') as folder:
        page = pathlib.Path(folder) / 'writer.html'
        page.write_text(fixture(page), encoding='utf-8')
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0),
            functools.partial(http.server.SimpleHTTPRequestHandler, directory=folder))
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            for width in (1280, 390):
                result = subprocess.run([chrome, '--headless', '--no-sandbox', '--disable-gpu',
                    '--no-proxy-server', '--virtual-time-budget=5000', '--dump-dom',
                    f'--window-size={width},1000', f'--user-data-dir={folder}/profile-{width}',
                    f'http://127.0.0.1:{server.server_port}/writer.html'],
                    capture_output=True, text=True, timeout=45)
                if result.returncode or 'data-result="passed"' not in result.stdout:
                    raise SystemExit(f'{width}px browser test failed:\n{result.stdout[-5000:]}\n{result.stderr[-1000:]}')
                print(f'PASS: {width}px production writer CSS, edit workflow and responsive display')
        finally:
            server.shutdown()
            server.server_close()


if __name__ == '__main__':
    main()
