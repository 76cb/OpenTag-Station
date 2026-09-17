#include "web/web_assets.hpp"
namespace opentag::web::assets {
const char writer_javascript[] = R"WRITER((function(){
window.OpenTagWriter={bind:function(){const {byId,asObject,asArray,first,setText,setValue,valueOf,showToast,api,load,submitMutation,PRIORITY}=window.OpenTagWriterHost;
  const writerState = { snapshot: {}, busy: false, spool: 0, filament: 0, vendor: 0, offset: 0, community: null, matches: [] };
  function renderWriter(data) {
    writerState.snapshot = asObject(data);
    const v = writerState.snapshot;
    setText('writer-progress', String(v.phase || 'idle') + ' — ' + String(v.message || '') +
      (v.total_blocks ? ' (' + v.completed_blocks + '/' + v.total_blocks + ' blocks)' : ''));
    const display=Object.assign({},v);
    ['current','proposed'].forEach(function(key){if(v[key])display[key]=Object.fromEntries(Object.entries(v[key]).filter(function(pair){return pair[1]!==null&&(!Array.isArray(pair[1])||pair[1].length); }));});
    setText('writer-detail', 'Current / Proposed: absent fields are not encoded. Zero is a real value.\n'+JSON.stringify(display, null, 2));
    ['confirm', 'import', 'retry'].forEach(function (name) {
      const n = byId('writer-' + name); if (!n) return;
      n.hidden = v.phase !== ({confirm:'preview', import:'import_preview', retry:'association_pending'})[name];
      n.disabled = writerState.busy;
    });
    if (v.phase === 'catalog') {
      writerState.offset = Number(v.next_offset || 0);
      byId('writer-next').disabled = !v.has_more;
      writerResults(asArray(v.items), v.entity);
    }
    if (v.phase === 'imported' && v.filament) {
      writerState.filament = Number(v.filament.id); writerState.spool = 0;
      setValue('writer-source', 'spoolman'); setValue('writer-entity', 'spool');
      setText('writer-selection', 'Canonical filament #' + writerState.filament + ': ' + String(v.filament.name || '') + '. Create or browse spools.');
    }
    if (v.spool && Number(v.spool.id) > 0) {
      writerState.spool = Number(v.spool.id);writerState.filament = Number(asObject(v.spool.filament).id);
      setText('writer-selection', 'Spool #' + writerState.spool + ': ' + String(asObject(v.spool.filament).name || ''));
    }
  }
  async function writerCommand(body) {
    if (writerState.busy) return;
    writerState.busy = true;
    let refreshing = false;
    try {
      await submitMutation('/tag-writer', { body: body, operationTimeoutMs: 180000,
        onProgress: function () {
          if (!refreshing) { refreshing = true;
            api('/tag-writer', {priority: PRIORITY.CONTROL}).then(renderWriter).catch(function () {}).finally(function () { refreshing = false; });
          }
        }
      });
    } catch (error) { showToast(error.message, true); }
    finally { writerState.busy = false; await load('/tag-writer', renderWriter, false, PRIORITY.CONTROL); }
  }
  function writerResults(items, entity) {
    const list = byId('writer-results'); if (!list) return; list.replaceChildren();
    items.forEach(function (item) {
      const button = document.createElement('button'); button.type = 'button'; button.className = 'button';
      const f = entity === 'spool' ? asObject(item.filament) : item;
      button.textContent = '#' + item.id + ' ' + String(first(f.manufacturer, asObject(f.vendor).name, '')) + ' ' + String(f.name || '') + ' ' + String(f.material || '') +
        (entity === 'spool' ? ' · remaining ' + String(first(item.remaining_weight, 'unknown')) + ' g' : '');
      button.addEventListener('click', function () {
        if (writerState.busy) return;
        if (entity === 'community') {
          const body={action:'import_preview', contract:'spoolmandb-community/0a39c9b5', entry:item};
          if(new TextEncoder().encode(item.name).length>64){const name=window.prompt('Spoolman allows 64 bytes for a name. Enter an explicit shorter display name; the source identity stays unchanged.','');if(!name)return;body.import_name=name;}
          writerCommand(body);return;
        }
        if (entity === 'vendor') {writerState.vendor = Number(item.id);setValue('writer-entity','filament');writerSearch(false);return;}
        writerState.filament = Number(f.id); writerState.spool = entity === 'spool' ? Number(item.id) : 0;
        setText('writer-selection', (writerState.spool ? 'Spool #' + writerState.spool : 'Filament #' + writerState.filament) + ': ' + String(f.name || ''));
        if (entity === 'filament') {setValue('writer-entity','spool');writerSearch(false);}
      });
      list.appendChild(button);
    });
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
  async function writerSearch(next) {
    if(writerState.busy)return;
    if(valueOf('writer-source')==='community') {
      try {
        if(!writerState.community){
          if(!writerState.communityLoading)writerState.communityLoading=communityCatalog().finally(function(){writerState.communityLoading=null;});
          writerState.community=await writerState.communityLoading;
        }
        if(valueOf('writer-source')!=='community')return;
        const q=valueOf('writer-search').toLowerCase(), material=valueOf('writer-material').toLowerCase();
        writerState.matches=writerState.community.filter(function (item) {return (!material||item.material.toLowerCase().includes(material))&&(!q||JSON.stringify(item).toLowerCase().includes(q));});
        const offset=next?writerState.offset:0;writerResults(writerState.matches.slice(offset,offset+8),'community');writerState.offset=offset+8;
        byId('writer-next').disabled=writerState.offset>=writerState.matches.length;
        setText('writer-progress',writerState.matches.length+' Community matches. Null means unknown; select a result for import preview.');
      } catch(error){showToast(error.message,true);}return;
    }
    await writerCommand({action:'catalog',entity:valueOf('writer-entity')||'spool',offset:next?writerState.offset:0,search:valueOf('writer-search'),
      material:valueOf('writer-material'),vendor_id:writerState.vendor,filament_id:writerState.filament});
  }
  function bindWriter() {
    byId('writer-panel').innerHTML = "<article id=\"writer-content\" class=\"card\">\n        <h3>Write an OpenPrintTag</h3>\n        <p>Spoolman is the source of truth. Import Community filaments before creating or selecting the physical spool.</p>\n        <label>Source <select id=\"writer-source\"><option value=\"spoolman\">My Spoolman</option><option value=\"community\">SpoolmanDB Community</option></select></label>\n        <label>Browse <select id=\"writer-entity\"><option value=\"spool\">Spools</option><option value=\"filament\">Filaments</option><option value=\"vendor\">Vendors</option></select></label>\n        <label>Search <input id=\"writer-search\" maxlength=\"64\" placeholder=\"Name; Community also searches vendor, material, SKU and metadata\"></label>\n        <label>Material <input id=\"writer-material\" maxlength=\"64\"></label>\n        <button id=\"writer-search-button\" class=\"button\" type=\"button\">Search / Refresh</button>\n        <button id=\"writer-next\" class=\"button\" type=\"button\" disabled>Next page</button>\n        <p id=\"writer-selection\" aria-live=\"polite\">Choose a spool, or choose a filament to create a spool.</p>\n        <div id=\"writer-results\"></div>\n        <details><summary>Create a physical Spoolman spool from selected filament</summary>\n          <form id=\"writer-create-form\">\n            <label>Initial net weight (g) <input name=\"initial_weight\" type=\"number\" min=\"0\" step=\"any\"></label>\n            <label>Used weight (g) <input name=\"used_weight\" type=\"number\" min=\"0\" step=\"any\"></label>\n            <label>Empty spool / tare (g) <input name=\"spool_weight\" type=\"number\" min=\"0\" step=\"any\"></label>\n            <label>Purchase price (Spoolman currency) <input name=\"price\" type=\"number\" min=\"0\" step=\"any\"></label>\n            <label>Location <input name=\"location\" maxlength=\"64\"></label>\n            <label>Lot / batch <input name=\"lot_nr\" maxlength=\"64\"></label>\n            <label>Notes <textarea name=\"comment\" maxlength=\"1024\"></textarea></label>\n            <button class=\"button\" type=\"submit\">Create spool</button>\n          </form>\n        </details>\n        <button id=\"writer-preview\" class=\"button\" type=\"button\">Preview initialize / rewrite</button>\n        <button id=\"writer-update\" class=\"button\" type=\"button\">Update consumed weight from Spoolman</button>\n        <p id=\"writer-progress\" role=\"status\" aria-live=\"polite\">No write requested</p>\n        <pre id=\"writer-detail\" style=\"white-space:pre-wrap;overflow-wrap:anywhere\"></pre>\n        <button id=\"writer-import\" class=\"button\" type=\"button\" hidden>Confirm import to Spoolman</button>\n        <button id=\"writer-confirm\" class=\"button warning\" type=\"button\" hidden>Confirm this exact tag write</button>\n        <button id=\"writer-retry\" class=\"button\" type=\"button\" hidden>Retry association (no tag rewrite)</button>\n      </article>";
    byId('writer-open').addEventListener('click',function(){byId('writer-panel').hidden=false;
      const selected=asObject(window.OpenTagWriterHost.state.spool);if(Number(selected.id)>0){writerState.spool=Number(selected.id);setText('writer-selection','Current associated spool #'+selected.id+' selected. Preview reads its live canonical data.');}
      load('/tag-writer',renderWriter,false);writerSearch(false);});
    byId('writer-search-button').addEventListener('click',function(){writerSearch(false);});
    byId('writer-next').addEventListener('click',function(){writerSearch(true);});
    byId('writer-source').addEventListener('change',function(){writerState.offset=0;writerState.vendor=0;writerState.filament=0;});
    ['preview','update'].forEach(function(name){byId('writer-'+name).addEventListener('click',function(){writerCommand({action:'preview',spool_id:writerState.spool,mode:name==='update'?'update':'rewrite'});});});
    byId('writer-import').addEventListener('click',function(){writerCommand({action:'import',import_token:writerState.snapshot.import_token});});
    byId('writer-confirm').addEventListener('click',function(){const p=writerState.snapshot;if(p.phase!=='preview'||!window.confirm('Write '+p.mode+' to UID '+p.uid+' for spool #'+p.spool_id+'? Target '+p.target_checksum+'. '+(p.previous_spool_id>0?'MOVE this NFC UID from spool #'+p.previous_spool_id+' to #'+p.spool_id+'. Previous spool UUID is retained. ':'')+'Keep tag and power in place.'))return;
      writerCommand({action:'write',uid:p.uid,generation:p.generation,target_checksum:p.target_checksum,spool_id:p.spool_id,previous_spool_id:p.previous_spool_id});});
    byId('writer-retry').addEventListener('click',function(){writerCommand({action:'retry_association'});});
    byId('writer-create-form').addEventListener('submit',function(event){event.preventDefault();const spool={filament_id:writerState.filament};
      new FormData(event.target).forEach(function(value,key){if(String(value).trim()!=='')spool[key]=['location','lot_nr','comment'].includes(key)?String(value):Number(value);});
      if(window.confirm('Create this physical spool in Spoolman?'))writerCommand({action:'create_spool',spool:spool});});
  }

Object.assign(window.OpenTagWriter,{writerState,renderWriter,writerCommand,validateCommunity,bindWriter,writerSearch});
if (!window.__OPENTAG_TEST__) bindWriter();
}};
})();)WRITER";
const std::size_t writer_javascript_size=sizeof(writer_javascript)-1;
static_assert(sizeof(writer_javascript)<=20U*1024U);
}
