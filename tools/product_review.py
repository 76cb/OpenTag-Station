"""Create deterministic, offline review pages using the shipped browser assets."""
from pathlib import Path
import argparse,json
import test_writer_display as harness
from check_public_privacy import assert_public_text, scan

REVIEW=r"""
window.OpenTagWriter.bind();window.OpenTagWriter.bindWriter();
const A=window.__OpenTagTest,W=window.OpenTagWriter;
A.bindProduct();A.bindWeighAndClear();
document.querySelectorAll('[data-nav]').forEach(n=>n.addEventListener('click',e=>{e.preventDefault();A.activateProductPage(n.dataset.nav);}));
A.renderConfig(fixtureConfig);
A.state.printers=[{id:'prusa-xl',display_name:"Prusa XL",state:'idle',revision:7,toolheads:Array.from({length:5},(_,i)=>({backend_id:i,assigned_spool_id:i===1?27:i===3?31:null}))}];
A.state.printerRevision=7;
const tag={read_only:true,state:'openprinttag',available:true,bringup_state:'ready',present:true,uid:preview.uid,decode:'pass',material_name:'PLA+ 2.0 Black',brand_name:'SUNLU',material_abbreviation:'PLA+',nominal_full_weight:1000,actual_full_weight:1000,remaining_weight:712,primary_color:[20,24,26,255],inventory:{tag_count:1,present:true,uid:preview.uid},geometry:{block_count:80,block_size:4}};
const workflow={stage:'spool_ready',openprinttag_available:true,spool_generation:3,tag,spool:{id:28,display_name:'PLA+ 2.0 Black',vendor:'SUNLU',material:'PLA+',remaining_grams:712,initial_grams:1000,primary_color:{red:20,green:24,blue:26}}};
A.renderNfc(tag);A.renderSpool({workflow});
A.renderScale({weigh_sync:weighResult,revision:1,adc_ready:true,calibrated:true,tare_ready:true,stable:true,samples_in_filter:3,measurement:{state:'completed',last_completed_grams:842,last_completed_age_ms:1000},profile:{id:'yzc-133-5kg',rated_capacity_grams:5000}});
A.renderPrinters();A.activateProductPage('home');
id('fixture-status').hidden=true;id('health-badge').textContent='Station ready';id('live-status').textContent='Station connected';id('live-indicator').className='status-dot online';
const scene=new URLSearchParams(location.search).get('scene')||'dashboard';
async function review(){
if(scene==='empty'){A.renderNfc({present:false,available:true,bringup_state:'ready',inventory:{present:false}});A.renderSpool({workflow:{stage:'awaiting_spool'}});}
if(scene==='weigh')A.openProductDialog('weigh-dialog');
if(scene==='assignment'){A.openAssignment();id('assign-tools').querySelectorAll('button')[2].click();}
if(scene==='manage')A.openProductDialog('manage-dialog');
if(scene==='blank'||scene==='unlinked'){
 const blank=scene==='blank',current={...tag,blank_compatible:blank,state:blank?'blank_compatible':'openprinttag',decode:blank?'blank':'pass',material_name:blank?'':tag.material_name,lifecycle:blank?'BLANK_COMPATIBLE':'OPENPRINTTAG_UNLINKED'};
 A.renderNfc(current);A.renderSpool({workflow:{...workflow,tag:current,spool:null,tag_lifecycle:current.lifecycle}});A.openProductDialog('manage-dialog');
}
if(scene==='reuse'){A.renderClear({phase:'cleared',uid:tag.uid,message:'Tag cleared and verified. Ready to reuse.'});id('clear-dialog').showModal();}

if(scene==='inventory') {A.activateProductPage('inventory');await new Promise(r=>setTimeout(r,80));}
if(scene==='printer')A.activateProductPage('printer');
if(scene==='writer'||scene==='preview'){await W.openModal();if(scene==='preview'){W.selected(spool,spool.filament);W.renderWriter(preview);}}
if(scene==='settings'){A.activateProductPage('settings');A.selectSettings('integrations');}
if(scene.startsWith('settings-')){A.activateProductPage('settings');A.selectSettings(scene.slice(9));}
if(scene==='clear'){await A.openClear();}
document.documentElement.dataset.review='ready';
}
review().catch(e=>{document.documentElement.dataset.review='failed';id('fixture-status').hidden=false;id('fixture-status').textContent=e.stack;});
"""

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    harness.CHECKS=REVIEW
    page=args.output/'index.html';content=harness.fixture(page);assert_public_text(content);page.write_text(content,encoding='utf-8')
    scenes=['empty','dashboard','weigh','assignment','manage','inventory','printer','writer','preview','settings','clear','blank','unlinked','reuse']
    extra=['settings-station','settings-scale','settings-network','settings-display','settings-advanced']
    sections=[]
    for width in [1440,1280,1024,768,390]:
        names=scenes+(extra if width in [1440,390] else [])
        sections.append(f'<h2>{width}px browser</h2><div class="grid">'+''.join(f'<a href="{width}-{s}.png"><img loading="lazy" src="{width}-{s}.png" alt="{s}"><span>{s}</span></a>' for s in names)+'</div>')
    touch_scenes=['empty','home','weigh','assign','settings']+list(json.loads((Path(__file__).resolve().parents[1]/'.pio/touch-flow-fixtures.json').read_text(encoding='utf-8')))
    sections.append('<h2>WT32 layout approximations</h2><p>Production coordinates; approximate fonts. These are not LVGL framebuffers or physical hardware captures.</p><div class="grid">'+''.join(f'<a href="wt32-{s}.png"><img src="wt32-{s}.png" alt="{s}"><span>{s}</span></a>' for s in touch_scenes)+'</div>')
    (args.output/'REVIEW.html').write_text('<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>OpenTag UI review</title><style>body{background:#101416;color:#f3f5f3;font:16px system-ui;margin:40px}h1,h2{font-weight:550}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:24px}a{color:#72dfbe;text-decoration:none}img{width:100%;display:block;border:1px solid #34433f}span{display:block;padding:12px}</style><h1>OpenTag Station · Current spool experience</h1><p>Offline fixtures using the shipped browser assets. Open a capture for full resolution. No station is connected.</p>'+''.join(sections)+'</html>',encoding='utf-8')
    scan([args.output])
if __name__=='__main__':main()
