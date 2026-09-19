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
from release_version import version
from check_public_privacy import assert_public_text

from web_asset_compression import browser_assets, writer_assets

BOOTSTRAP = r"""
window.__OPENTAG_TEST__=true;window.cspFetch=window.fetch.bind(window);
const communityRow={id:'brand-pla',name:'Community PLA',manufacturer:'Brand',material:'PLA',density:1.24,diameter:1.75,weight:1000,spool_weight:130};
const fixtureConfig={revision:7,reconciliation:{auto_update_after_weigh:false,normal_tolerance_grams:5,warning_tolerance_grams:20},device:{hostname:'opentag-station',brightness_percent:80},wifi:{ssid:'Workshop',password_configured:true},spoolman:{url:'',authentication_token_configured:false},filabridge:{url:'',selected_printer_id:'',authentication_token_configured:false},web:{access_token_configured:false},scale_profile:{id:'yzc-133-5kg',rated_capacity_grams:5000,overload_ratio:1.1},toolheads:[]};
window.fetch=async(path,options={})=>{if(window.fixtureReply&&!String(path).endsWith('/config'))return window.fixtureReply(path,options);if(!String(path).endsWith('/config'))throw Error('Fixture blocked unexpected request: '+path);return new Response(JSON.stringify({api_version:'v1',ok:true,data:fixtureConfig}),{headers:{'Content-Type':'application/json'}});};
"""
HOST = r"""
const originalHost=window.OpenTagWriterHost;
const spool={id:28,initial_weight:1000,remaining_weight:1000,used_weight:0,spool_weight:130,archived:false,
filament:{id:22,name:'Sunlu PLA+ 2.0 Black',material:'PLA+',weight:777.12,density:1.24,diameter:1.75,color_hex:'000000',vendor:{id:5,name:'Sunlu'}}};
const preview={phase:'preview',mode:'rewrite',spool_id:28,previous_spool_id:0,uid:'E004000000000028',tag_type:'NXP ICODE SLIX2',block_count:80,block_size:4,generation:'3',current_checksum:'9E639911',target_checksum:'B57D9186',changed_blocks:[2,3,7],total_blocks:3,instance_uuid:'fea84b36-1234-4234-9234-123456789012',current:{},proposed:{brand_name:'Sunlu',material_name:'Sunlu PLA+ 2.0 Black',material_abbreviation:'PLA+',nominal_netto_full_weight:1000,actual_netto_full_weight:1000,empty_container_weight:130,density:1.24,filament_diameter:1.75,consumed_weight:0,primary_color:[0,0,0,255]},warnings:['Writing is not atomic. Keep tag and power in place.','missing recommended GTIN','missing recommended manufactured date','missing recommended min_print_temperature']};
let result={phase:'idle'},hold=null;const requests=[];
const id=n=>document.getElementById(n);
window.OpenTagWriterHost={...originalHost,byId:id,showToast:m=>id('fixture-status').textContent=m,api:async()=>structuredClone(result),submitMutation:async(path,{body,onProgress})=>{
requests.push(body);if(hold)await hold;
if(body.action.startsWith('update_')){const entity=body.action.slice(7),current=entity==='spool'?spool:spool.filament;
if(!body.expected||Object.keys(body.changes).some(k=>!(k in body.expected)||(current[k]??null)!==body.expected[k])){result={phase:'failed',edit_conflict:true,[entity]:structuredClone(current)};throw Error('Spoolman changed since this editor was opened.');}}
if(body.action==='catalog'){const items=body.search==='none'?[]:body.offset?[{...spool,id:40}]:[{...spool,id:27,remaining_weight:0,used_weight:1000},spool,...Array.from({length:6},(_,i)=>({...spool,id:30+i}))];result={phase:'catalog',entity:'spool',items,offset:body.offset||0,next_offset:(body.offset||0)+items.length,has_more:items.length===8};}
if(body.action==='preview')result={...preview,spool_id:spool.id,spool:structuredClone(spool)};
if(body.action==='community_status')result={phase:'community_catalog',catalog_state:'ready',catalog_version:'2026-09-18',catalog_records:53424,catalog_size:2430373,message:'Community catalog ready'};
if(body.action==='community_search')result={phase:'community',items:[communityRow],offset:body.offset||0,next_offset:(body.offset||0)+1,has_more:false,catalog_version:'2026-09-18'};
if(body.action==='community_update')result={phase:'community_catalog',catalog_state:'ready',catalog_version:'2026-09-18',catalog_records:53424,catalog_size:2430373,message:'Community catalog ready'};
if(body.action==='community_select')result={phase:'import_preview',import_token:'fixture-import',vendor_name:'Brand',proposed_filament:communityRow};
if(body.action==='import')result={phase:'imported',filament:{...communityRow,id:88,vendor:{id:5,name:'Brand'}}};
if(body.action==='catalog'&&body.entity==='filament')result={phase:'catalog',entity:'filament',items:[structuredClone(spool.filament)],offset:0,has_more:false};
if(body.action==='catalog'&&body.filament_id===88)result={phase:'catalog',entity:'spool',items:[],offset:0,has_more:false};
if(body.action==='create_spool'){Object.assign(spool,body.spool,{id:29,filament:structuredClone(spool.filament)});result={phase:'spool_selected',spool:structuredClone(spool)};}
if(body.action==='update_filament'){Object.assign(spool.filament,body.changes);result={phase:'updated',spool:structuredClone(spool),filament:structuredClone(spool.filament),message:'Saved and verified in Spoolman.'};}
if(body.action==='update_spool'){Object.assign(spool,body.changes);spool.remaining_weight=Math.max(0,spool.initial_weight-spool.used_weight);result={phase:'updated',spool:structuredClone(spool),message:'Saved and verified in Spoolman.'};}
if(body.action==='clear_preview')result={phase:'clear_preview',mode:'clear',uid:preview.uid,generation:'3',current_checksum:'AA',target_checksum:'BB',material_name:spool.filament.name,changed_blocks:[0,1,2]};
if(body.action==='clear')result={...result,phase:'unlink_pending',message:'Tag is blank and verified. Spoolman unlink is still pending.'};
if(body.action==='retry_unlink')result={...result,phase:'cleared',message:'Tag cleared and verified. Ready to reuse.'};
if(body.action==='write')result={...preview,phase:'association_pending',message:'Tag verified; Spoolman association pending.'};
if(body.action==='retry_association')result={...preview,phase:'complete',message:'Tag and Spoolman association verified (fixture only).'};
}};
let weighResult={measurement_id:8,phase:'ready',spool_id:28,name:spool.filament.name,gross:842,tare:130,measured:712,canonical_remaining:720,difference:-8,can_update:true,policy_auto:false};
window.fixtureReply=async(path,options)=>{let data={};if(options.method==='POST'){const body=JSON.parse(options.body);if(String(path).endsWith('/scale/update')){requests.push(body);weighResult={...weighResult,phase:'updated',can_update:false,canonical_remaining:712,message:'Spoolman updated and verified'};}else await window.OpenTagWriterHost.submitMutation(path,{body});data={operation_id:42};}else if(String(path).includes('/operations/'))data={id:42,state:'succeeded'};else if(String(path).endsWith('/tag-writer'))data=result;else if(String(path).endsWith('/scale'))data={weigh_sync:weighResult};return new Response(JSON.stringify({api_version:'v1',ok:true,data}),{status:options.method==='POST'?202:200,headers:{'Content-Type':'application/json'}});};
"""
CHECKS = r"""
window.OpenTagWriter.bind();window.OpenTagWriter.bindWriter();const T=window.OpenTagWriter,A=window.__OpenTagTest;A.bindProduct();A.bindWeighAndClear();
document.querySelectorAll('[data-nav]').forEach(n=>n.addEventListener('click',e=>{e.preventDefault();A.activateProductPage(n.dataset.nav);}));
A.renderConfig(fixtureConfig);
A.renderDevice({device:{hostname:'OpenTag Station',hardware_id:'WT32-SC01 Plus',local_url:'http://station.example'},build:{version:'__PROJECT_VERSION__',git_sha:'fixture',build_date:'2026-09-17'}});
A.renderNetwork({system:{network:{connected:true,rssi_dbm:-48,provisioning:{active:false}}},networks:[]});
A.renderScale({revision:1,adc_ready:true,calibrated:true,tare_ready:true,stable:true,samples_in_filter:3,measurement:{state:'completed',last_completed_grams:1130,last_completed_age_ms:1000},profile:{id:'yzc-133-5kg',rated_capacity_grams:5000}});
A.renderPrinters();A.renderDiagnostics({status:'Fixture only',free_heap_bytes:65000});A.renderLogs({items:[]});
const settle=async()=>{for(let i=0;i<20;i++)await Promise.resolve();};
const shown=n=>getComputedStyle(id(n)).display!=='none';
async function checks(){let count=0;const check=(v,m)=>{if(!v)throw Error(m);++count;};
const tag={read_only:true,state:'openprinttag',available:true,bringup_state:'ready',present:true,uid:preview.uid,decode:'pass',material_name:spool.filament.name,material_abbreviation:'PLA+',nominal_full_weight:1000,actual_full_weight:1000,consumed_weight:0,remaining_weight:1000,inventory:{tag_count:1,present:true,uid:preview.uid},geometry:{block_count:80,block_size:4}};
A.renderNfc(tag);A.renderSpool({workflow:{stage:'spool_ready',openprinttag_available:true,tag,spool:{id:28,display_name:spool.filament.name}}});
A.activateProductPage('tags');check(id('nfc-summary').textContent==='OpenPrintTag recognized','tag title');check(id('nfc-association').textContent.includes('Spool #28'),'canonical association');check(shown('nfc-identity-row'),'linked identity shown');
A.renderSpool({workflow:{stage:'spool_not_found',openprinttag_available:true,tag}});check(!shown('nfc-identity-row'),'no blank Identity row');check(id('nfc-association').textContent==='Not linked','valid tag is not automatically linked');
A.renderSpool({workflow:{stage:'spool_ready',openprinttag_available:true,tag,spool:{id:28,display_name:spool.filament.name}}});
check(!id('writer-dialog').open,'writer closed initially');id('writer-open').click();await settle();const dialog=id('writer-dialog');check(dialog.open&&dialog.matches(':modal'),'native modal opened');check(dialog.getAttribute('aria-labelledby')==='writer-title','dialog name');check(dialog.contains(document.activeElement),'focus inside modal');
check(getComputedStyle(document.body).overflow==='hidden','background scroll locked');id('refresh-all').focus();check(dialog.contains(document.activeElement),'native modal makes background inert');check(id('writer-continue').disabled,'Continue requires physical spool');
check(!shown('writer-preview'),'inactive preview no layout');const bounds=dialog.getBoundingClientRect();check(bounds.width<=innerWidth&&bounds.height<=innerHeight,'dialog fits viewport');check(innerWidth>520?Math.abs((innerWidth-bounds.width)/2-bounds.left)<3:bounds.width===innerWidth,'centered desktop or full-screen narrow');
check(id('writer-continue').getBoundingClientRect().bottom<=innerHeight,'footer available without body scroll');
const rows=id('writer-results').children;rows[1].click();await new Promise(r=>setTimeout(r,250));check(rows[1].getAttribute('aria-pressed')==='true','selected aria');check(rows[1].textContent.includes('✓'),'visible checkmark');check(getComputedStyle(rows[1]).backgroundColor!==getComputedStyle(rows[0]).backgroundColor,'selected contrast');check(getComputedStyle(rows[1].querySelector('.writer-swatch')).backgroundColor==='rgb(0, 0, 0)','CSP-safe swatch');
check(!id('writer-continue').disabled,'Continue enabled');id('writer-next').click();await settle();check(id('writer-page').textContent==='Page 2','Next page');check(id('writer-next').disabled,'last page');check(T.writerState.spool===28,'selection persists off page');id('writer-previous').click();await settle();check(id('writer-previous').disabled,'Previous first page');
id('writer-continue').click();check(shown('writer-selection-pane')&&!shown('writer-inventory'),'Review step');check(id('writer-selected-title').textContent.includes('Sunlu'),'selected name');check(id('writer-step-2').getAttribute('aria-current')==='step','step announced');
id('writer-edit-filament').click();check(id('writer-editor-warning').textContent.includes('every Spoolman spool'),'shared warning');const weight=id('writer-editor-fields').querySelector('[name="weight"]');check(weight.value==='777.12','nominal snapshot');weight.value='1000';await T.saveEditor();check(T.writerState.material.weight===1000,'verified save');check(T.writerState.step===2,'save stays in Review');check(id('writer-progress').textContent.includes('Saved and verified in Spoolman'),'persistent save result');
id('writer-edit-spool').click();const used=id('writer-editor-fields').querySelector('[name="used_weight"]');used.value='25';spool.used_weight=15;await T.saveEditor();check(spool.used_weight===15,'conflict no PATCH');check(used.value==='25','draft retained');check(id('writer-editor-message').textContent.includes('Used weight (g): 15'),'fresh value visible');check(id('writer-editor-message').textContent.includes('your draft 25'),'draft visible beside fresh');check(!shown('writer-confirm'),'stale exact write hidden');await T.saveEditor();check(spool.used_weight===25,'explicit resave');check(!shown('writer-editor'),'editor closes after verification');
let release;hold=new Promise(r=>release=r);const reading=T.writerCommand({action:'preview',spool_id:28});check(shown('writer-activity'),'reading busy card');check(id('writer-activity-title').textContent==='Reading tag','purposeful reading');check(id('writer-close').disabled,'busy close disabled');dialog.dispatchEvent(new Event('cancel',{cancelable:true}));check(dialog.open,'busy Escape refused');release();hold=null;await reading;
check(shown('writer-review'),'preview visible');check(id('writer-detail').textContent.includes('NXP ICODE SLIX2'),'tag type retained in Advanced');check(id('writer-diff').textContent.includes('1000 g'),'readable proposal');check(id('writer-tag').textContent.includes('E0:04:00:00:00:00:00:28'),'readable UID');check(!id('writer-advanced').open,'Advanced collapsed');check(!id('writer-notices').open,'metadata collapsed');check(id('writer-critical').textContent.includes('incomplete tag'),'critical warning prominent');check(shown('writer-confirm')&&!shown('writer-import')&&!shown('writer-retry'),'one applicable action');
hold=new Promise(r=>release=r);const writing=T.writerCommand({action:'write',uid:preview.uid,generation:preview.generation,target_checksum:preview.target_checksum,spool_id:28,previous_spool_id:0});T.renderWriter({...preview,phase:'writing',completed_blocks:2,total_blocks:3});
check(T.writerState.step===4,'Write step');check(id('writer-meter').value===2&&id('writer-meter').max===3,'block progress');check(id('writer-activity-detail').textContent.includes('2 / 3'),'block count text');check(id('writer-close').disabled&&id('writer-back').disabled,'write navigation locked');dialog.dispatchEvent(new Event('cancel',{cancelable:true}));T.closeModal();check(dialog.open,'write cannot dismiss');check(!shown('writer-inventory'),'inventory unavailable during write');release();hold=null;await writing;
check(T.writerState.snapshot.phase==='association_pending','pending link');check(id('writer-critical').textContent.includes('without rewriting'),'no rewrite instruction');check(shown('writer-retry')&&!shown('writer-confirm'),'association only action');check(id('nfc-association').textContent.includes('pending'),'tag page pending link');id('writer-retry').click();await settle();check(requests.filter(r=>r.action==='write').length===1,'retry did not rewrite');check(T.writerState.step===5,'Verified step');check(id('writer-review-title').textContent.includes('written and verified'),'strong success');check(['Tag readback', 'Verified', 'OpenPrintTag decode', 'Valid', 'Spoolman link'].every(t=>id('writer-diff').textContent.includes(t)),'three independent verification results');check(shown('writer-done'),'Done action');check(id('writer-tag').textContent.includes('Sunlu'),'success retains product');
id('writer-done').click();check(!dialog.open,'Done closes');check(document.activeElement===id('writer-open'),'focus restored');check(id('manage-dialog').open,'returns to tag management');id('manage-dialog').close();document.body.classList.remove('modal-open');
await T.openModal();await settle();dialog.dispatchEvent(new Event('cancel',{cancelable:true}));check(!dialog.open,'idle Escape closes');
await T.openModal();await settle();id('writer-search').value='none';await T.writerSearch(false);check(id('writer-results').textContent.includes('No matching spools'),'empty state');
for(const phase of ['idle','failed','preview','association_pending','complete']){T.writerState.invalidated=false;T.renderWriter({...preview,phase});for(const [name,active] of [['confirm','preview'],['import','import_preview'],['retry','association_pending']]){const node=id('writer-'+name);node.hidden=false;check((getComputedStyle(node).display!=='none')===(phase===active),'CSS action visibility '+name+' '+phase);}}
T.renderWriter({phase:'failed',message:'Tag moved. Place the same tag back on the reader.'});check(id('writer-progress').textContent.includes('Tag moved'),'failure visible in workflow');check(shown('writer-back'),'safe recovery navigation');T.closeModal();

// Production sources and failed-read recovery at every real viewport width.
T.writerState.snapshot={phase:'idle'};T.writerState.step=1;await T.openModal();
check(!document.querySelector('[data-writer-source="community"]'),'disabled source absent');
check(!document.querySelector('#writer-source option[value="community"]'),'disabled source option absent');
check(!requests.some(r=>r.action.startsWith('community_')),'no automatic catalog requests');
T.selected(spool,spool.filament);T.writerState.step=2;T.writerState.lastAction='preview';T.writerState.previewRequest={action:'preview',spool_id:28,mode:'rewrite'};
T.renderWriter({phase:'reading'});T.renderWriter({phase:'failed',message:'Place exactly one stable NFC-V tag'});
check(T.writerState.spool===28,'failed preview retains spool');check(shown('writer-retry-read'),'failed preview offers Retry Read');check(!shown('writer-activity'),'failure clears busy');
id('writer-back').click();check(T.writerState.step===2&&T.writerState.spool===28,'Back to Review retains spool');
id('writer-back').click();await settle();check(T.writerState.step===1,'Back to Select');check(id('writer-source').value==='spoolman','active source retained');check(!id('writer-results').textContent.includes('Community'),'no stale source text');T.closeModal();
const policy=document.querySelector('meta[http-equiv="Content-Security-Policy"]').content,connect=policy.match(/connect-src ([^;]+)/)[1];check(!connect.includes('github.io')&&!connect.includes('*')&&!connect.split(' ').includes('https:'),'Community CSP remains station local');
const violations=[];document.addEventListener('securitypolicyviolation',e=>violations.push(e.blockedURI));await window.cspFetch('https://unrelated.invalid/opentag-fixture').catch(()=>{});await new Promise(r=>setTimeout(r,25));check(violations.some(v=>v.includes('unrelated.invalid')),'real CSP denies unrelated fetch');
A.activateProductPage('scale');A.renderWeighSync(weighResult);check(id('weigh-policy').textContent.includes('OFF'),'manual policy');check(!id('weigh-update').disabled,'manual weight update offered');check(id('weigh-measured').textContent.includes('712'),'gross minus tare calculation');await A.updateWeighedSpool();check(requests.at(-1).measurement_id===8,'manual update bound to explicit measurement');check(id('weigh-message').textContent.includes('updated and verified'),'manual update result');A.renderWeighSync({...weighResult,policy_auto:true,automatic:true,phase:'ready',can_update:true});check(id('weigh-policy').textContent.includes('ON'),'auto policy');check(id('weigh-update').hidden,'auto ready does not offer duplicate update');A.renderWeighSync({...weighResult,phase:'conflict',can_update:false,message:'Spoolman usage changed. No inventory value was overwritten.'});check(id('weigh-update').disabled&&id('weigh-message').textContent.includes('No inventory'),'weight concurrency conflict');
A.activateProductPage('tags');result={phase:'idle'};await A.openClear();check(id('clear-dialog').open&&id('clear-dialog').matches(':modal'),'Clear native modal');check(id('clear-summary').textContent.includes(preview.uid),'clear exact UID');check(id('clear-effects').textContent.includes('78–79'),'clear preserved tail explanation');check(!id('clear-confirm').disabled,'clear deliberate second step');A.renderClear({...result,phase:'clearing',total_blocks:23,completed_blocks:14});id('clear-dialog').dispatchEvent(new Event('cancel',{cancelable:true}));check(id('clear-dialog').open,'clear busy Escape locked');check(id('clear-message').textContent.includes('14 / 23'),'clear verified block progress');A.renderClear({...result,phase:'clear_preview'});await A.clearCommand('clear');check(id('clear-retry').hidden===false,'unlink pending retry');check(id('clear-message').textContent.includes('blank and verified'),'blank physical result retained');A.renderClear({...result,phase:'unlink_pending',cleanup_stage:'local_identity',message:'Confirmed local identity changed; unlink refused'});check(id('clear-message').textContent.includes('Confirmed local identity changed; unlink refused'),'local cleanup backend reason visible');check(id('clear-message').textContent.includes('Local station identity cleanup'),'local cleanup distinguished from remote');A.renderClear({...result,phase:'unlink_pending',cleanup_stage:'journal',message:'Recovery journal cleanup is still pending'});check(id('clear-message').textContent.includes('Recovery record cleanup'),'journal cleanup distinguished');const last=requests.length;await A.clearCommand('retry_unlink');check(requests.length===last+1&&requests.at(-1).action==='retry_unlink','retry sends only unlink');check(id('clear-title').textContent.includes('verified'),'clear success');check(id('clear-dialog').getBoundingClientRect().width<=innerWidth,'clear fits viewport');A.closeClear();check(document.activeElement===id('writer-open')||document.activeElement===id('clear-open'),'clear focus restoration');

for(const page of ['home','scale','printer','tags','settings']){A.activateProductPage(page);check(document.documentElement.scrollWidth<=innerWidth,'no horizontal page overflow '+page+' '+JSON.stringify([...document.querySelectorAll('main *')].filter(n=>n.getBoundingClientRect().right>innerWidth&&n.getBoundingClientRect().width>0).map(n=>({id:n.id,tag:n.tagName,cls:n.className,right:n.getBoundingClientRect().right})).slice(0,15)));}
A.activateProductPage('tags');check(document.querySelector('#diagnostics-json').closest('details')!==null,'diagnostics disclosed');

window.OpenTagWriterHost.closeProductDialog('manage-dialog');
const lifecycleUid='E004000000000001';
const blankTag={present:true,uid:lifecycleUid,blank_compatible:true,read_only:true,state:'blank_compatible',decode:'blank',available:true,bringup_state:'ready',inventory:{present:true,tag_count:1,uid:lifecycleUid},lifecycle:'BLANK_COMPATIBLE'};
A.state.clearSnapshot={};A.renderNfc(blankTag);A.renderSpool({workflow:{tag:blankTag,spool:null,tag_lifecycle:'BLANK_COMPATIBLE'}});
check(!id('writer-open').hidden&&id('writer-open').textContent==='Assign tag to a spool','blank primary Assign');
check(id('tag-update').hidden&&id('clear-open').hidden,'blank hides update and clear');
check(id('nfc-badge').textContent==='Blank'&&!id('nfc-summary').textContent.includes('Unsupported'),'blank never Unsupported');
check(id('home-action-label').textContent==='Assign tag'&&!id('home-weigh').disabled,'blank Home assignment independent of scale');
const validTag={...blankTag,blank_compatible:false,decode:'pass',state:'openprinttag',lifecycle:'OPENPRINTTAG_UNLINKED'};
A.renderNfc(validTag);A.renderSpool({workflow:{tag:validTag,spool:null,tag_lifecycle:'OPENPRINTTAG_UNLINKED'}});
check(id('tag-update').hidden&&!id('writer-open').hidden&&!id('clear-open').hidden&&id('writer-open').textContent==='Link to a spool','unlinked Link and Clear');
A.renderSpool({workflow:{tag:validTag,spool:{id:28},openprinttag_available:true,tag_lifecycle:'OPENPRINTTAG_LINKED'}});
check(!id('tag-update').hidden&&!id('writer-open').hidden&&!id('clear-open').hidden&&id('writer-open').textContent==='Reassign','linked Update Reassign Clear');
A.renderClear({phase:'cleared',uid:lifecycleUid});check(!id('clear-assign').hidden,'clear success immediate Assign');
A.renderClear({phase:'unlink_pending',uid:lifecycleUid,cleanup_stage:'local_identity',message:'Local persistence failed'});check(id('clear-message').textContent.includes('Local persistence failed')&&id('clear-assign').hidden,'pending preserves reason and prevents premature assign');
id('fixture-status').textContent='PASS: '+count+' real-browser assertions at '+innerWidth+'px';id('fixture-status').dataset.result='passed';
parent.postMessage({fixtureResult:id('fixture-status').textContent,result:'passed'},location.origin);
}
checks().catch(e=>{id('fixture-status').textContent='FAIL: '+e.message;id('fixture-status').dataset.result='failed';parent.postMessage({fixtureResult:id('fixture-status').textContent,result:'failed'},location.origin);});
"""


def fixture(page):
    css, core = browser_assets()
    writer = b"".join(writer_assets()).decode("utf-8")
    files = {'writer-fixture.css': css.decode("utf-8"), 'fixture-bootstrap.js': BOOTSTRAP,
             'writer-core.js': core.decode('utf-8'), 'writer-host.js': HOST, 'writer-production.js': writer, 'writer-checks.js': CHECKS}
    for name, content in files.items():
        content = content.replace('__PROJECT_VERSION__', version())
        assert_public_text(content, name)
        (page.parent / name).write_text(content, encoding='utf-8')
    root = pathlib.Path(__file__).resolve().parents[1]
    server = root / 'src/web/local_web_server.cpp'
    policy = re.search(r'\{"Content-Security-Policy",(.*?)\}', server.read_text(), re.S)[1]
    policy = ''.join(re.findall(r'"([^"]*)"', policy))
    html = (root / 'src/web/web_assets.cpp').read_text().split('R"HTML(',1)[1].split(')HTML";',1)[0]
    html = html.replace(')HTML" OPENTAG_GIT_SHA R"HTML(', 'fixture')
    from product_features import community_enabled
    html = html.replace(')HTML" OPENTAG_COMMUNITY_VALUE R"HTML(', str(int(community_enabled())))
    html = re.sub(r'<script.*?</script>', '', html)
    html = re.sub(r'<link rel="stylesheet".*?>', '<link rel="stylesheet" href="writer-fixture.css">', html)
    html = html.replace('<meta charset="utf-8">', '<meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="'+policy+'">')
    html = html.replace('<main id="content">', '<main id="content"><p id="fixture-status" role="status">Browser-only fixture · no station connection</p>')
    return html.replace('</body>', ''.join('<script src="'+name+'"></script>' for name in ['fixture-bootstrap.js','writer-core.js','writer-production.js','writer-host.js','writer-checks.js'])+'</body>')


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
        # Modern headless Chrome clamps narrow top-level windows. An exact-width
        # frame gives the production document a real 390px layout viewport.
        # Only the harness is outside the station CSP; the child retains it.
        (page.parent / 'frame-result.js').write_text("""
addEventListener('message',e=>{if(e.origin!==location.origin||e.source!==document.querySelector('iframe').contentWindow||!e.data.fixtureResult)return;
const result=document.getElementById('fixture-status');result.textContent=e.data.fixtureResult;result.dataset.result=e.data.result;});
""", encoding='utf-8')
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0),
            functools.partial(http.server.SimpleHTTPRequestHandler, directory=folder))
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            for width in (1440, 1280, 1024, 768, 390):
                (page.parent / 'frame.html').write_text(
                    '<!doctype html><html><body style="margin:0"><p id="fixture-status">Running</p>'
                    '<script src="frame-result.js"></script>'
                    f'<iframe title="Production UI fixture" src="writer.html" style="border:0;width:{width}px;height:1000px"></iframe></body></html>',
                    encoding='utf-8')
                profile = f'{folder}/profile-{width}'
                if os.name == 'posix' and chrome.lower().endswith('.exe'):
                    profile = subprocess.check_output(
                        ['wslpath', '-w', profile], text=True).strip()
                result = subprocess.run([chrome, '--headless', '--no-sandbox', '--disable-gpu',
                    '--no-proxy-server', '--host-resolver-rules=MAP unrelated.invalid 127.0.0.1', '--virtual-time-budget=12000', '--dump-dom',
                    f'--window-size={max(width,800)},1100', f'--user-data-dir={profile}',
                    f'http://127.0.0.1:{server.server_port}/frame.html'],
                    capture_output=True, text=True, timeout=45)
                if result.returncode or 'data-result="passed"' not in result.stdout or f'assertions at {width}px' not in result.stdout:
                    raise SystemExit(f'{width}px browser test failed (exit {result.returncode}):\n{result.stdout[-5000:]}\n{result.stderr[-1000:]}')
                status = re.search(r'PASS: \d+ real-browser assertions at \d+px', result.stdout)[0]
                print(status)
        finally:
            server.shutdown()
            server.server_close()


if __name__ == '__main__':
    main()
