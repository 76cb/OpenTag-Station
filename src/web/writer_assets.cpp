#include "web/web_assets.hpp"
namespace opentag::web::assets {
const char writer_javascript[] = R"WRITER((function(){
window.OpenTagWriter={bind:function(){const {byId,asObject,asArray,first,setText,setValue,valueOf,showToast,api,submitMutation,PRIORITY}=window.OpenTagWriterHost;
  const writerState={snapshot:{},busy:false,spool:0,filament:0,vendor:0,filterFilament:0,offset:0,start:0,hasMore:false,community:null,matches:[],items:[],entity:'spool',selected:null,material:null,editor:null,invalidated:false};
  const S=writerState, fields=window.OpenTagWriterFields;
  const fmt=(v,unit='')=>v===null||v===undefined||v===''?'—':String(v)+unit;
  function el(tag,text,cls){const n=document.createElement(tag);if(text!==undefined)n.textContent=text;if(cls)n.className=cls;return n;}
  function visible(id,show){const n=byId(id);n.hidden=!show;n.className=n.className.split(/\s+/).filter(x=>x&&x!=='writer-hidden').concat(show?[]:['writer-hidden']).join(' ');}
  function swatch(v){const color=Array.isArray(v)?v.map(n=>Number(n).toString(16).padStart(2,'0')).join(''):String(v||'');const n=el('span','', 'writer-swatch');if(/^(?:[a-f\d]{6}|[a-f\d]{8})$/i.test(color)){n.style.backgroundColor='#'+color;n.setAttribute('aria-label','Color #'+color);n.title='#'+color;}else n.hidden=true;return n;}
  function values(target,pairs){const dl=el('dl',undefined,'writer-values');pairs.forEach(p=>{dl.append(el('dt',p[0]),el('dd',fmt(p[1],p[2]||'')));});target.append(dl);}
  function controls(){
    const locked=S.busy||!!S.communityLoading||S.snapshot.phase==='association_pending';
    ['source','entity','search','material','search-button','edit-spool','edit-filament','save','cancel','create-submit'].forEach(k=>byId('writer-'+k).disabled=locked);
    byId('writer-entity').disabled=locked||valueOf('writer-source')==='community';
    byId('writer-previous').disabled=locked||S.start===0;
    byId('writer-next').disabled=locked||!S.hasMore;
    ['preview','update'].forEach(k=>byId('writer-'+k).disabled=locked||!S.spool||!!S.editor);
    ['confirm','import','retry'].forEach(k=>{const phase={confirm:'preview',import:'import_preview',retry:'association_pending'}[k];visible('writer-'+k,S.snapshot.phase===phase&&(k!=='confirm'||!S.invalidated));byId('writer-'+k).disabled=S.busy||!!S.editor;});
    S.rows?.forEach(n=>n.disabled=locked);
    document.querySelectorAll('#writer-panel input, #writer-panel textarea, #writer-panel select, #writer-filters button').forEach(n=>{n.disabled=locked||(n.id==='writer-entity'&&valueOf('writer-source')==='community');});
    byId('writer-content').setAttribute('aria-busy',String(S.busy));
  }
  function invalidate(){S.invalidated=true;S.snapshot={phase:'idle',message:'Selection or data changed. Generate a new tag preview.'};visible('writer-review',false);setText('writer-detail','No current tag preview.');setText('writer-progress',S.snapshot.message);controls();}
  function details(){
    const s=S.selected,f=S.material,d=byId('writer-selected-detail');d.replaceChildren();
    setText('writer-selection',s?'SELECTED · SPOOL #'+s.id:f?'SELECTED · FILAMENT #'+f.id:'NO SPOOL SELECTED');
    setText('writer-selected-title',f?String(f.name||'Unnamed filament'):'Choose a spool to continue');
    if(f){d.append(swatch(f.color_hex),el('span',String(asObject(f.vendor).name||'')+' · '+fmt(f.material)));
      if(s)values(d,[['Remaining',s.remaining_weight,' g'],['Initial',s.initial_weight,' g'],['Used',s.used_weight,' g'],['Tare',s.spool_weight,' g'],['Status',s.archived?'Archived':'Active']]);
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
    });markRows();controls();
  }
  function buildFields(id,schema,record,prefix){const d=byId(id);d.replaceChildren();schema.forEach(([key,label,max])=>{const l=el('label',label),input=el(key==='comment'?'textarea':'input');input.id='writer-'+prefix+'-'+key;input.name=key;input.value=record[key]??'';input.type=max?'number':'text';if(max){input.min=['density','diameter','weight'].includes(key)?'0.001':'0';input.max=String(max);input.step=key.startsWith('settings_')?'1':'any';}else input.maxLength=key==='comment'?1024:64;l.append(input);d.append(l);});}
  function openEditor(kind){if(S.busy)return;const record=kind==='spool'?S.selected:S.material;if(!record)return;S.editor={kind,record:JSON.parse(JSON.stringify(record))};invalidate();setText('writer-editor-title',kind==='spool'?'Edit this physical spool':'Edit shared filament definition');setText('writer-editor-warning',kind==='spool'?'Changes apply only to Spool #'+record.id+'.':'Changes to filament #'+record.id+' affect every Spoolman spool using this filament.');setText('writer-editor-message','');buildFields('writer-editor-fields',fields[kind],record,'edit');visible('writer-editor',true);controls();}
  function closeEditor(){S.editor=null;visible('writer-editor',false);controls();}
  function formValues(id,schema,original){const out={};schema.forEach(([key,,max])=>{const input=byId(id).querySelector('[name="'+key+'"]');const raw=String(input.value).trim();if(raw===''&&(max||original[key]==null))return;let v=max?Number(raw):raw;if(v===original[key])return;if(key==='color_hex')v=v.toUpperCase();if(max&&(!Number.isFinite(v)||v<Number(input.min)||v>max||(key.startsWith('settings_')&&!Number.isInteger(v))))throw new Error('Check '+key+' value');if(!max&&new TextEncoder().encode(v).length>(key==='comment'?1024:64))throw new Error(key+' exceeds the Spoolman text limit');if(key==='color_hex'&&!/^(?:[a-f\d]{6}|[a-f\d]{8})$/i.test(v))throw new Error('Color must be 6 or 8 hex digits');if(v!==original[key]&&!(v===''&&original[key]==null))out[key]=v;});return out;}
  async function saveEditor(){if(!S.editor||S.busy)return;const edit=S.editor;try{const changes=formValues('writer-editor-fields',fields[edit.kind],edit.record);if(!Object.keys(changes).length){setText('writer-editor-message','No changed values to save.');return;}const expected=Object.fromEntries(Object.keys(changes).map(k=>[k,({...edit.record,...edit.expected})[k]??null]));const body={action:'update_'+edit.kind,changes,expected};body[edit.kind+'_id']=edit.record.id;if(edit.kind==='filament'&&S.spool)body.spool_id=S.spool;if(await writerCommand(body))closeEditor();else{const v=S.snapshot,fresh=v[edit.kind];if(v.edit_conflict&&fresh){setText('writer-editor-message','Spoolman changed. Current values: '+fields[edit.kind].filter(([k])=>k in changes).map(([k,label])=>label+': '+fmt(fresh[k])).join('; ')+'. Your draft is unchanged. Review these values, then Save Changes again.');edit.expected={...edit.expected,...Object.fromEntries(Object.keys(changes).map(k=>[k,fresh[k]??null]))};}else setText('writer-editor-message','Save was not verified. Refresh before retrying.');}}catch(e){setText('writer-editor-message',e.message);}}
  function preview(v){const active=['preview','association_pending','complete','import_preview'].includes(v.phase)&&!(v.phase==='preview'&&S.invalidated);visible('writer-review',active);if(!active)return;
    const tag=byId('writer-tag');tag.replaceChildren();const diff=byId('writer-diff');diff.replaceChildren();const critical=byId('writer-critical');critical.replaceChildren();const notices=byId('writer-notice-list');notices.replaceChildren();
    setText('writer-review-title',v.phase==='import_preview'?'Review Community import':v.phase==='complete'?'Tag and association verified':v.phase==='association_pending'?'Tag verified · association pending':'Review OpenPrintTag changes');
    if(v.phase==='import_preview'){values(tag,[['Source','COMMUNITY — NOT YET IN SPOOLMAN'],['Vendor',v.vendor_name],['Product',asObject(v.proposed_filament).name],['Material',asObject(v.proposed_filament).material],['Nominal weight',asObject(v.proposed_filament).weight,' g'],['Density',asObject(v.proposed_filament).density,' g/cm³'],['Diameter',asObject(v.proposed_filament).diameter,' mm']]);}
    else{values(tag,[['UID',v.uid],['Mode',v.mode],['Spool','#'+v.spool_id],['OpenPrintTag UUID',v.instance_uuid],['Current checksum',v.current_checksum],['Target checksum',v.target_checksum],['Changed blocks',asArray(v.changed_blocks).length],['Preserved','78–79']]);
      const table=el('table',undefined,'writer-diff'),head=el('tr');['Field','Current','Proposed'].forEach(t=>head.append(el('th',t)));const thead=el('thead');thead.append(head);table.append(thead);const tbody=el('tbody');
      window.OpenTagWriterDiffFields.forEach(([key,label,unit])=>{const a=asObject(v.current)[key],b=asObject(v.proposed)[key],row=el('tr',undefined,JSON.stringify(a)!==JSON.stringify(b)?'writer-changed':'');row.append(el('th',label));[a,b].forEach(value=>{const cell=el('td',key==='primary_color'?(Array.isArray(value)?'#'+value.map(n=>Number(n).toString(16).padStart(2,'0')).join(''):'—'):fmt(value,unit||''));if(key==='primary_color')cell.append(swatch(value));row.append(cell);});tbody.append(row);});table.append(tbody);diff.append(table);
    }
    const warn=text=>critical.append(el('p',text,'writer-warning'));
    if(v.previous_spool_id>0)warn('MOVE this NFC UID from Spool #'+v.previous_spool_id+' to Spool #'+v.spool_id+'. The previous spool UUID is retained.');
    else if(v.repurpose)warn('Repurpose: this write replaces the tag’s current spool identity.');
    if(v.recovering_interrupted_write)warn('Recovery: this is an interrupted write. Confirm an explicit rewrite only after reviewing the recovered tag.');
    const optional=asArray(v.warnings).filter(w=>/^missing recommended /i.test(w));
    const metadata=asArray(v.warnings).filter(w=>!String(w).includes('association will move')&&!/not atomic|Full rewrite/i.test(w));asArray(v.warnings).filter(w=>/not atomic|Full rewrite/i.test(w)).forEach(warn);
    metadata.forEach(w=>notices.append(el('li',String(w))));visible('writer-notices',metadata.length>0);setText('writer-notice-count',optional.length?optional.length+' optional metadata fields are not populated':'Metadata notices');
  }
  function renderWriter(data){const v=asObject(data);S.snapshot=v;
    if(v.phase==='catalog'){const match=asArray(v.items).find(i=>v.entity==='spool'&&Number(i.id)===S.spool);if(match)selected(match,match.filament);writerResults(asArray(v.items),v.entity);page(asArray(v.items),Number(v.offset||0),!!v.has_more);S.offset=Number(v.next_offset||0);}
    if(v.phase==='imported'&&v.filament){S.communitySelected=null;writerResults([],'spool');page([],0,false);selected(null,v.filament);S.filterFilament=Number(v.filament.id);S.filamentName=v.filament.name;setValue('writer-source','spoolman');setValue('writer-entity','spool');filters();}
    if(['preview','updated','spool_selected'].includes(v.phase)){if(v.spool&&Number(v.spool.id)>0){selected(v.spool,v.spool.filament);if(v.phase==='updated'&&S.entity==='spool')writerResults(S.items.map(i=>Number(i.id)===S.spool?v.spool:i),'spool');}else if(v.phase==='updated'&&v.filament)selected(null,v.filament);}
    setText('writer-progress',String(v.message||(window.OpenTagWriterProgress[v.phase]||v.phase||'Select a spool to begin.'))+(v.phase==='writing'?' ('+v.completed_blocks+'/'+v.total_blocks+' blocks)':''));
    setText('writer-detail',JSON.stringify(v,null,2));preview(v);controls();
  }
  async function writerCommand(body){if(S.busy)return false;S.busy=true;if(body.action==='preview')S.invalidated=false;if(body.action.startsWith('update_'))invalidate();controls();let refreshing=false,success=false,accepting=true;const editing=body.action.startsWith('update_');
    try{await submitMutation('/tag-writer',{body,operationTimeoutMs:180000,onProgress:()=>{if(!refreshing){refreshing=true;api('/tag-writer',{priority:PRIORITY.CONTROL}).then(v=>{if(accepting&&(!editing||['editing','failed','association_pending'].includes(v.phase)))renderWriter(v);}).catch(()=>{}).finally(()=>refreshing=false);}}});success=true;}catch(error){showToast(error.message,true);}
    finally{accepting=false;try{const v=await api('/tag-writer',{priority:PRIORITY.CONTROL});const entity=body.action.slice(7);if(editing&&(!success||v.phase!=='updated'||Number(asObject(v[entity]).id)!==body[entity+'_id'])){success=false;renderWriter(v.phase==='failed'&&v.edit_conflict&&Number(asObject(v[entity]).id)===body[entity+'_id']?v:{phase:'failed',message:v.message||'Edit readback was not verified; refresh before retrying.'});}else renderWriter(v);}catch(error){success=false;showToast(error.message,true);}S.busy=false;controls();}return success&&S.snapshot.phase!=='failed';
  }
  function validateCommunity(data) {
    if (!Array.isArray(data) || data.length > 100000) throw new Error('Unsupported Community catalog shape/size');
    const ids = new Set();
    data.forEach(function (item) {
      if (!item || typeof item !== 'object' || ['id','manufacturer','name','material'].some(function (k) { return typeof item[k] !== 'string' || !item[k] || item[k].length > (k === 'id' ? 180 : 128); }) ||
          !Number.isFinite(item.density) || !Number.isFinite(item.diameter) || ids.has(item.id)) throw new Error('Malformed Community catalog or duplicate identity');
      ids.add(item.id);
    });
    return data;
  }
  async function communityCatalog() {
    // Public compiled source used by the upstream UI. Catalog storage lives in
    // the browser, never the station's internal RAM or backend JSON allocator.
    const controller = new AbortController();const timer = window.setTimeout(function () {controller.abort();},30000);
    try {
      const r = await fetch('https://icezaza2543.github.io/SpoolmanDB-Community/filaments.json', {signal:controller.signal,cache:'no-cache'});
      if (!r.ok || !r.body || Number(r.headers.get('Content-Length')) > 67108864) throw new Error('Community catalog unavailable or oversized');
      const reader = r.body.getReader();const chunks=[];let count=0;
      for (;;) {const part=await reader.read();if(part.done)break;count+=part.value.byteLength;if(count>67108864){await reader.cancel();throw new Error('Community catalog exceeds 64 MiB bound');}chunks.push(part.value);}
      const bytes=new Uint8Array(count);let offset=0;chunks.forEach(function (chunk){bytes.set(chunk,offset);offset+=chunk.length;});
      return validateCommunity(JSON.parse(new TextDecoder().decode(bytes)));
    } finally {window.clearTimeout(timer);}
  }

  async function writerSearch(direction){if(S.busy||S.snapshot.phase==='association_pending')return;const sig=JSON.stringify([valueOf('writer-source'),valueOf('writer-entity'),valueOf('writer-search'),valueOf('writer-material'),S.vendor,S.filterFilament]);const start=direction==='previous'?Math.max(0,S.start-8):direction===true?S.offset:direction==='refresh'&&sig===S.query?S.start:0;S.query=sig;
    if(valueOf('writer-source')==='community'){try{if(!S.community){if(!S.communityLoading)S.communityLoading=communityCatalog().finally(()=>{S.communityLoading=null;controls();});controls();S.community=await S.communityLoading;}if(valueOf('writer-source')!=='community')return;const q=valueOf('writer-search').toLowerCase(),m=valueOf('writer-material').toLowerCase();S.matches=S.community.filter(i=>(!m||i.material.toLowerCase().includes(m))&&(!q||JSON.stringify(i).toLowerCase().includes(q)));const items=S.matches.slice(start,start+8);writerResults(items,'community');page(items,start,start+8<S.matches.length,S.matches.length);}catch(error){showToast(error.message,true);}return;}
    await writerCommand({action:'catalog',entity:valueOf('writer-entity')||'spool',offset:start,search:valueOf('writer-search'),material:valueOf('writer-material'),vendor_id:S.vendor,filament_id:S.filterFilament});
  }
  function bindWriter(){if(window.CSSStyleSheet){const sheet=new CSSStyleSheet();sheet.replaceSync(window.OpenTagWriterCss);document.adoptedStyleSheets=[...document.adoptedStyleSheets,sheet];}byId('writer-panel').innerHTML=window.OpenTagWriterLayout;
    byId('writer-open').addEventListener('click',()=>{byId('writer-panel').hidden=false;if(S.snapshot.phase==='association_pending'){controls();return;}writerSearch(false);});
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
Object.assign(window.OpenTagWriter,{writerState,renderWriter,writerCommand,validateCommunity,bindWriter,writerSearch,writerResults,openEditor,saveEditor,selected,filters});
if(!window.__OPENTAG_TEST__)bindWriter();
}};
})();)WRITER"
#include "writer_layout.inc"
;
const std::size_t writer_javascript_size=sizeof(writer_javascript)-1;
static_assert(sizeof(writer_javascript)<=32U*1024U);
}
