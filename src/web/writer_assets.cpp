#include "web/web_assets.hpp"
namespace opentag::web::assets {
const char writer_javascript[] = R"WRITER((function(){
window.OpenTagWriter={bind:function(){const {byId,asObject,asArray,first,setText,setValue,valueOf,showToast,api,submitMutation,PRIORITY,validateCommunity,communityCatalog,tagStatus}=window.OpenTagWriterHost;
  const writerState={snapshot:{},busy:false,spool:0,filament:0,vendor:0,filterFilament:0,offset:0,start:0,hasMore:false,community:null,matches:[],items:[],entity:'spool',selected:null,material:null,editor:null,invalidated:false,step:1};
  const S=writerState, fields=window.OpenTagWriterFields;
  const {fmt,el,visible,swatch,values}=window.OpenTagWriterUi(byId);
  const {buildFields,formValues}=window.OpenTagWriterForms(byId,el);
  function controls(){
    const locked=lockedModal()||!!S.communityLoading||S.snapshot.phase==='association_pending';
    ['source','entity','search','material','search-button','edit-spool','edit-filament','save','cancel','create-submit'].forEach(k=>byId('writer-'+k).disabled=locked);
    byId('writer-entity').disabled=locked||valueOf('writer-source')==='community';
    byId('writer-previous').disabled=locked||S.start===0;
    byId('writer-next').disabled=locked||!S.hasMore;
    ['preview','update'].forEach(k=>byId('writer-'+k).disabled=locked||!S.spool||!!S.editor);
    ['confirm','import','retry'].forEach(k=>{const phase={confirm:'preview',import:'import_preview',retry:'association_pending'}[k];visible('writer-'+k,S.snapshot.phase===phase&&(k!=='confirm'||!S.invalidated));byId('writer-'+k).disabled=S.busy||!!S.editor;});
    S.rows?.forEach(n=>n.disabled=locked);
    document.querySelectorAll('#writer-panel input, #writer-panel textarea, #writer-panel select, #writer-filters button').forEach(n=>{n.disabled=locked||(n.id==='writer-entity'&&valueOf('writer-source')==='community');});
    byId('writer-content').setAttribute('aria-busy',String(S.busy));wizard();
  }
  function invalidate(){S.invalidated=true;S.snapshot={phase:'idle',message:'Selection or data changed. Generate a new tag preview.'};visible('writer-review',false);setText('writer-detail','No current tag preview.');setText('writer-progress',S.snapshot.message);controls();}
  function details(){
    const s=S.selected,f=S.material,d=byId('writer-selected-detail');d.replaceChildren();
    setText('writer-selection',s?'SELECTED · SPOOL #'+s.id:f?'SELECTED · FILAMENT #'+f.id:'NO SPOOL SELECTED');
    setText('writer-selected-title',f?String(f.name||'Unnamed filament'):'Choose a spool to continue');
    if(f){d.append(swatch(f.color_hex),el('span',String(asObject(f.vendor).name||'')+' · '+fmt(f.material)));
      if(s)values(d,[['Remaining',s.remaining_weight,' g'],['Initial',s.initial_weight,' g'],['Used',s.used_weight,' g'],['Tare',s.spool_weight,' g'],['Location',s.location],['Lot',s.lot_nr],['Status',s.archived?'Archived':'Active']]);
      d.append(el('h4','Filament definition #'+f.id));values(d,[['Nominal weight',f.weight,' g'],['Default tare',f.spool_weight,' g'],['Diameter',f.diameter,' mm'],['Density',f.density,' g/cm³'],['Color',f.color_hex]]);
    }
    visible('writer-edit-spool',!!s);visible('writer-edit-filament',!!f);visible('writer-create',!!f&&!s);
    if(f&&!s)buildFields('writer-create-fields',fields.spool,{},'create');
    controls();
  }
  function selected(spool,filament){S.selected=spool||null;S.material=filament||asObject(spool?.filament);if(!S.material.id)S.material=null;S.spool=Number(spool?.id||0);S.filament=Number(S.material?.id||0);details();markRows();}
  function markRows(){S.rows?.forEach(n=>{const chosen=n.dataset.entity==='spool'?Number(n.dataset.id)===S.spool:n.dataset.entity==='filament'?Number(n.dataset.id)===S.filament:n.dataset.entity==='community'&&n.dataset.id===S.communitySelected;n.className='writer-row'+(chosen?' writer-selected':'');n.setAttribute('aria-pressed',String(chosen));n.children[0].textContent=chosen?'✓':'○';});}
  function filters(){const d=byId('writer-filters');d.replaceChildren();[['vendor',S.vendorName],['filterFilament',S.filamentName]].forEach(([key,name])=>{if(!S[key])return;const b=el('button',(key==='vendor'?'Vendor: ':'Filament: ')+(name||'#'+S[key])+' ×','button');b.type='button';b.setAttribute('aria-label','Clear '+(key==='vendor'?'vendor':'filament')+' filter');b.addEventListener('click',()=>{if(S.busy)return;S[key]=0;if(key==='vendor')S.filterFilament=0;filters();writerSearch(false);});d.append(b);});}
  function page(items,start,more,total){S.start=start;S.offset=start+items.length;S.hasMore=more;setText('writer-page','Page '+(Math.floor(start/8)+1));setText('writer-range',items.length?'Showing '+(start+1)+'–'+(start+items.length)+(total!==undefined?' of '+total:''):(total===0?'No matching items':'No items on this page'));controls();}
  function writerResults(items,entity){S.items=items;S.entity=entity;S.rows=[];const list=byId('writer-results');list.replaceChildren();
    items.forEach(item=>{const f=entity==='spool'?asObject(item.filament):item,b=el('button',undefined,'writer-row');b.type='button';b.dataset.id=String(item.id);b.dataset.entity=entity;b.append(el('span','○','writer-tick'));const text=el('span');text.append(el('strong',(entity==='community'?'': '#'+item.id+' · ')+String(f.name||'Unnamed')));text.append(swatch(f.color_hex),el('small',String(first(f.manufacturer,asObject(f.vendor).name,''))+' · '+fmt(f.material)));
      if(entity==='spool')text.append(el('small',fmt(item.remaining_weight,' g')+' remaining · '+fmt(item.initial_weight,' g')+' initial'+(item.archived?' · ARCHIVED':'')));
      if(entity==='community')text.append(el('small','COMMUNITY — NOT YET IN SPOOLMAN'));
      b.append(text);b.addEventListener('click',()=>{if(S.busy||S.snapshot.phase==='association_pending')return;closeEditor();invalidate();
        if(entity==='community'){const body={action:'import_preview',contract:'spoolmandb-community/0a39c9b5',entry:item};if(new TextEncoder().encode(item.name).length>64){const name=window.prompt('Spoolman allows 64 bytes for a name. Enter an explicit shorter display name; source identity stays unchanged.','');if(!name)return;body.import_name=name;}selected(null,null);S.communitySelected=String(item.id);markRows();setText('writer-selection','SELECTED · COMMUNITY');setText('writer-selected-title',item.name);const d=byId('writer-selected-detail');d.append(el('p','COMMUNITY — NOT YET IN SPOOLMAN'));values(d,[['Vendor',item.manufacturer],['Material',item.material],['Nominal weight',item.weight,' g']]);writerCommand(body);return;}
        if(entity==='vendor'){S.vendor=Number(item.id);S.vendorName=item.name;S.filterFilament=0;selected(null,null);setValue('writer-entity','filament');filters();writerSearch(false);return;}
        selected(entity==='spool'?item:null,f);
        if(entity==='filament'){S.filterFilament=Number(item.id);S.filamentName=item.name;setValue('writer-entity','spool');filters();writerSearch(false);}
      });S.rows.push(b);list.append(b);
    });if(!items.length)list.append(el('p','No matching '+(entity==='community'?'Community filaments':entity+'s')+'. Try changing your search or material filter.','empty-state'));markRows();controls();
  }
  function openEditor(kind){if(S.busy)return;const record=kind==='spool'?S.selected:S.material;if(!record)return;S.step=2;S.editor={kind,record:JSON.parse(JSON.stringify(record))};invalidate();setText('writer-editor-title',kind==='spool'?'Edit this physical spool':'Edit shared filament definition');setText('writer-editor-warning',kind==='spool'?'Changes apply only to Spool #'+record.id+'.':'Changes to filament #'+record.id+' affect every Spoolman spool using this filament.');setText('writer-editor-message','');buildFields('writer-editor-fields',fields[kind],record,'edit');visible('writer-editor',true);controls();}
  function closeEditor(){S.editor=null;visible('writer-editor',false);controls();}
  async function saveEditor(){if(!S.editor||S.busy)return;const edit=S.editor;try{const changes=formValues('writer-editor-fields',fields[edit.kind],edit.record);if(!Object.keys(changes).length){setText('writer-editor-message','No changed values to save.');return;}const expected=Object.fromEntries(Object.keys(changes).map(k=>[k,({...edit.record,...edit.expected})[k]??null]));const body={action:'update_'+edit.kind,changes,expected};body[edit.kind+'_id']=edit.record.id;if(edit.kind==='filament'&&S.spool)body.spool_id=S.spool;if(await writerCommand(body))closeEditor();else{const v=S.snapshot,fresh=v[edit.kind];if(v.edit_conflict&&fresh){setText('writer-editor-message','Spoolman changed. Current values: '+fields[edit.kind].filter(([k])=>k in changes).map(([k,label])=>label+': '+fmt(fresh[k])+' → your draft '+fmt(changes[k])).join('; ')+'. Your draft is unchanged. Review these values, then Save Changes again.');edit.expected={...edit.expected,...Object.fromEntries(Object.keys(changes).map(k=>[k,fresh[k]??null]))};}else setText('writer-editor-message','Save was not verified. Refresh before retrying.');}}catch(e){setText('writer-editor-message',e.message);}}
  const preview=v=>window.OpenTagWriterPreview(v,S,{byId,asObject,asArray,setText,visible,values,el,fmt,swatch,uidText});
  function renderWriter(data){const v=asObject(data);S.snapshot=v;if(['reading','loading_spool','preview'].includes(v.phase))S.step=3;if(['validating','writing','verifying','decoding','associating','association_pending'].includes(v.phase))S.step=4;if(v.phase==='complete')S.step=5;if(['updated','spool_selected'].includes(v.phase))S.step=2;
    if(v.phase==='catalog'){const match=asArray(v.items).find(i=>v.entity==='spool'&&Number(i.id)===S.spool);if(match)selected(match,match.filament);writerResults(asArray(v.items),v.entity);page(asArray(v.items),Number(v.offset||0),!!v.has_more);S.offset=Number(v.next_offset||0);}
    if(v.phase==='imported'&&v.filament){S.communitySelected=null;writerResults([],'spool');page([],0,false);selected(null,v.filament);S.filterFilament=Number(v.filament.id);S.filamentName=v.filament.name;setValue('writer-source','spoolman');setValue('writer-entity','spool');filters();}
    if(['preview','updated','spool_selected'].includes(v.phase)){if(v.spool&&Number(v.spool.id)>0){selected(v.spool,v.spool.filament);if(v.phase==='updated'&&S.entity==='spool')writerResults(S.items.map(i=>Number(i.id)===S.spool?v.spool:i),'spool');}else if(v.phase==='updated'&&v.filament)selected(null,v.filament);}
    setText('writer-progress',String(v.message||(window.OpenTagWriterProgress[v.phase]||v.phase||'Select a spool to begin.'))+(v.phase==='writing'?' ('+v.completed_blocks+'/'+v.total_blocks+' blocks)':''));
    setText('writer-detail',JSON.stringify(v,null,2));preview(v);controls();tagStatus?.();
  }
  async function writerCommand(body){if(S.busy)return false;S.busy=true;if(body.action==='preview')S.invalidated=false;if(body.action.startsWith('update_'))invalidate();controls();let refreshing=false,success=false,accepting=true,failure='';const editing=body.action.startsWith('update_');if(body.action==='preview')S.step=3;if(['write','retry_association'].includes(body.action))S.step=4;renderWriter({phase:({preview:'reading',write:'writing',retry_association:'associating',catalog:'searching',import:'importing'})[body.action]||'editing',message:body.action==='preview'?'Reading complete tag and protection state…':window.OpenTagWriterStatus[body.action]||'Working…'});
    try{await submitMutation('/tag-writer',{body,operationTimeoutMs:180000,onProgress:()=>{if(!refreshing){refreshing=true;api('/tag-writer',{priority:PRIORITY.CONTROL}).then(v=>{if(accepting&&(!editing||['editing','failed','association_pending'].includes(v.phase)))renderWriter(v);}).catch(()=>{}).finally(()=>refreshing=false);}}});success=true;}catch(error){failure=error.message;showToast(error.message,true);}
    finally{accepting=false;try{const v=await api('/tag-writer',{priority:PRIORITY.CONTROL});const entity=body.action.slice(7);if(editing&&(!success||v.phase!=='updated'||Number(asObject(v[entity]).id)!==body[entity+'_id'])){success=false;renderWriter(v.phase==='failed'&&v.edit_conflict&&Number(asObject(v[entity]).id)===body[entity+'_id']?v:{phase:'failed',message:v.message||'Edit readback was not verified; refresh before retrying.'});}else renderWriter(!success&&failure&&!['failed','association_pending','complete','validating','writing','verifying','decoding','associating'].includes(v.phase)?{phase:'failed',message:failure}:v);}catch(error){success=false;renderWriter({phase:'failed',message:error.message});}S.busy=false;controls();}return success&&S.snapshot.phase!=='failed';
  }
  async function writerSearch(direction){if(S.busy||S.snapshot.phase==='association_pending')return;const sig=JSON.stringify([valueOf('writer-source'),valueOf('writer-entity'),valueOf('writer-search'),valueOf('writer-material'),S.vendor,S.filterFilament]);const start=direction==='previous'?Math.max(0,S.start-8):direction===true?S.offset:direction==='refresh'&&sig===S.query?S.start:0;S.query=sig;
    if(valueOf('writer-source')==='community'){try{if(!S.community){if(!S.communityLoading){setText('writer-progress','Loading Community catalog…');}if(!S.communityLoading)S.communityLoading=communityCatalog().finally(()=>{S.communityLoading=null;controls();});controls();S.community=await S.communityLoading;}if(valueOf('writer-source')!=='community')return;const q=valueOf('writer-search').toLowerCase(),m=valueOf('writer-material').toLowerCase();S.matches=S.community.filter(i=>(!m||i.material.toLowerCase().includes(m))&&(!q||JSON.stringify(i).toLowerCase().includes(q)));const items=S.matches.slice(start,start+8);writerResults(items,'community');page(items,start,start+8<S.matches.length,S.matches.length);}catch(error){showToast(error.message,true);}return;}
    await writerCommand({action:'catalog',entity:valueOf('writer-entity')||'spool',offset:start,search:valueOf('writer-search'),material:valueOf('writer-material'),vendor_id:S.vendor,filament_id:S.filterFilament});
  }
  function uidText(uid){return String(uid||'—').replace(/[^a-f0-9]/gi,'').match(/.{1,2}/g)?.join(':')||'—';}
  function lockedModal(){return S.busy||['validating','writing','verifying','decoding','associating'].includes(S.snapshot.phase);}
  function closeModal(){if(lockedModal())return;if(S.editor&&!window.confirm('Discard the unsaved editor draft?'))return;closeEditor();if(S.snapshot.phase!=='association_pending')invalidate();byId('writer-dialog').close();document.body.classList.remove('modal-open');byId('writer-open').focus();}
  async function openModal(){byId('writer-panel').hidden=false;byId('writer-dialog').showModal();document.body.classList.add('modal-open');S.step=S.snapshot.phase==='association_pending'?4:1;controls();byId('writer-search').focus();try{const v=await api('/tag-writer',{priority:PRIORITY.CONTROL});if(['association_pending','validating','writing','verifying','decoding','associating'].includes(v.phase)){renderWriter(v);return;}}catch(e){setText('writer-progress',e.message);}if(byId('writer-dialog').open&&!lockedModal())writerSearch('refresh');}
  function wizard(){const p=S.snapshot.phase,n=S.step,locked=lockedModal();
    for(let i=1;i<=5;i++){const node=byId('writer-step-'+i);node.setAttribute('aria-current',i===n?'step':'false');node.dataset.complete=String(i<n);}
    visible('writer-inventory',n===1);visible('writer-selection-pane',n===2||(n===1&&!!S.material&&!S.spool));
    ['preview','update'].forEach(k=>visible('writer-'+k,n===2&&!S.editor));visible('writer-continue',n===1&&p!=='import_preview');byId('writer-continue').disabled=locked||!S.spool;
    visible('writer-back',n>1&&(n<4||p==='failed'));byId('writer-back').disabled=locked;visible('writer-dismiss',n===1||n===4);['close','dismiss'].forEach(k=>byId('writer-'+k).disabled=locked);
    visible('writer-done',n===5);byId('writer-done').disabled=locked;visible('writer-check',locked&&!S.busy);visible('writer-advanced',n>=3);setText('writer-footer-selection',S.spool?'Spool #'+S.spool:'Select a physical spool');
    const busy=S.busy||['reading','loading_spool','validating','writing','verifying','decoding','associating'].includes(p);visible('writer-activity',busy);setText('writer-activity-title',window.OpenTagWriterStatus[p]||'Working…');setText('writer-activity-detail',n===4?'Keep the tag on the reader. Do not remove power.':S.snapshot.message||'Please wait.');
    const meter=byId('writer-meter');if(p==='writing'&&S.snapshot.total_blocks){meter.max=S.snapshot.total_blocks;meter.value=S.snapshot.completed_blocks||0;setText('writer-activity-detail',meter.value+' / '+meter.max+' blocks. Keep tag and power in place.');}else meter.removeAttribute('value');
    byId('writer-progress').className='result-banner '+(p==='failed'?'status-error':p==='updated'||p==='complete'?'status-success':p==='association_pending'?'status-warning':'');
    if(p==='failed'&&n>=3)setText('writer-progress',(S.snapshot.message||'Unable to complete this step')+' Keep the same tag nearby. Return to Review for a fresh safety preview.');if(n===2&&p==='catalog')setText('writer-progress','Review this spool, then preview the tag.');if(p==='updated')setText('writer-progress','✓ Saved to Spoolman. Generate a new tag preview.');
  }
  function bindWriter(){if(window.CSSStyleSheet){const sheet=new CSSStyleSheet();sheet.replaceSync(window.OpenTagWriterCss);document.adoptedStyleSheets=[...document.adoptedStyleSheets,sheet];}byId('writer-panel').innerHTML=window.OpenTagWriterLayout;
    byId('writer-open').addEventListener('click',openModal);['close','dismiss','done'].forEach(k=>byId('writer-'+k).addEventListener('click',closeModal));byId('writer-dialog').addEventListener('cancel',e=>{e.preventDefault();closeModal();});byId('writer-continue').addEventListener('click',()=>{if(S.spool&&!lockedModal()){S.step=2;controls();byId('writer-title').focus();}});byId('writer-back').addEventListener('click',()=>{if(!lockedModal()){S.step=S.step>=3?2:1;closeEditor();invalidate();byId('writer-title').focus();}});byId('writer-check').addEventListener('click',async()=>{try{renderWriter(await api('/tag-writer',{priority:PRIORITY.CONTROL}));}catch(e){setText('writer-progress',e.message);}});
    byId('writer-search-button').addEventListener('click',()=>writerSearch('refresh'));['next','previous'].forEach(k=>byId('writer-'+k).addEventListener('click',()=>writerSearch(k==='next'?true:'previous')));
    ['source','entity'].forEach(k=>byId('writer-'+k).addEventListener('change',()=>{if(S.busy)return;closeEditor();invalidate();S.start=0;S.offset=0;S.hasMore=false;if(k==='source'){S.communitySelected=null;S.vendor=0;S.filterFilament=0;selected(null,null);}else if(valueOf('writer-entity')!=='spool')S.filterFilament=0;filters();writerResults([],valueOf('writer-entity'));writerSearch(false);}));
    ['preview','update'].forEach(name=>byId('writer-'+name).addEventListener('click',()=>{if(S.spool&&!S.editor)writerCommand({action:'preview',spool_id:S.spool,mode:name==='update'?'update':'rewrite'});}));
    byId('writer-import').addEventListener('click',()=>{if(S.snapshot.phase==='import_preview')writerCommand({action:'import',import_token:S.snapshot.import_token});});
    byId('writer-confirm').addEventListener('click',()=>{const p=S.snapshot;if(p.phase!=='preview'||S.invalidated||S.busy||S.editor||!window.confirm('Write '+p.mode+' to UID '+p.uid+' for spool #'+p.spool_id+'? Target '+p.target_checksum+'. '+(p.previous_spool_id>0?'MOVE this NFC UID from spool #'+p.previous_spool_id+' to #'+p.spool_id+'. Previous spool UUID is retained. ':'')+'Keep tag and power in place.'))return;writerCommand({action:'write',uid:p.uid,generation:p.generation,target_checksum:p.target_checksum,spool_id:p.spool_id,previous_spool_id:p.previous_spool_id});});
    byId('writer-retry').addEventListener('click',()=>{if(S.snapshot.phase==='association_pending')writerCommand({action:'retry_association'});});
    ['spool','filament'].forEach(k=>byId('writer-edit-'+k).addEventListener('click',()=>openEditor(k)));
    byId('writer-editor').addEventListener('submit',event=>{event.preventDefault();saveEditor();});byId('writer-cancel').addEventListener('click',closeEditor);
    byId('writer-create-form').addEventListener('submit',event=>{event.preventDefault();if(S.busy||!S.filament)return;try{const spool=Object.assign({filament_id:S.filament},formValues('writer-create-fields',fields.spool,{}));if(window.confirm('Create this physical spool in Spoolman?'))writerCommand({action:'create_spool',spool});}catch(error){showToast(error.message,true);}});
    controls();
  }
Object.assign(window.OpenTagWriter,{openModal,closeModal,writerState,renderWriter,writerCommand,validateCommunity,bindWriter,writerSearch,writerResults,openEditor,saveEditor,selected,filters});
if(!window.__OPENTAG_TEST__)bindWriter();
}};
})();)WRITER"
#include "writer_layout.inc"
;
const std::size_t writer_javascript_size=sizeof(writer_javascript)-1;
static_assert(sizeof(writer_javascript)<=32U*1024U);
}
