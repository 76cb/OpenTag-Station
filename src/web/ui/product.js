// Presentation only. All commands go through the station's existing fenced handlers.
const productDialogs = new Map();
function openProductDialog(id) {
  const dialog = byId(id);
  if (!dialog || dialog.open) return;
  productDialogs.set(id, document.activeElement);
  dialog.showModal();
  document.body.classList.add('modal-open');
}
function closeProductDialog(id) {
  const dialog = byId(id);
  if (!dialog?.open || dialog.dataset.busy === 'true') return;
  dialog.close();
  if (!document.querySelector('dialog[open]')) document.body.classList.remove('modal-open');
  productDialogs.get(id)?.focus();
}
function spoolIdentity() {
  const original=state.currentTag||{}, w=state.tagWorkflow||{}, inv=original.inventory||{};
  const clear=state.clearSnapshot||{},cleared=String(clear.uid||'').replace(/:/g,'')===String(original.uid||inv.uid||'').replace(/:/g,'')&&['cleared','unlink_pending','unlinking'].includes(clear.phase);
  const t=cleared?{...original,blank_compatible:true,decode:'blank',material_name:'',brand_name:'',brand:''}:original;
  const uid=String(first(t.uid,inv.uid,'')).replace(/:/g,'');
  const present=first(t.present,inv.present,false)===true;
  const same=present&&uid&&uid===String(w.tag?.uid||'').replace(/:/g,'');
  return {t,w,present,spool:same&&!cleared&&w.openprinttag_available?w.spool:null};
}
function renderSpoolChoices(workflow) {
  const form=byId('confirm-spool-form');
  if(!form||byId('overview').dataset.bound!=='true')return;
  let list=byId('spool-choice-buttons');
  if(!list){list=productElement('div',null,'spool-choice-buttons');list.id='spool-choice-buttons';form.prepend(list);}
  list.replaceChildren();
  asArray(workflow.candidates).forEach(candidate=>{
    const button=makeButton('#'+candidate.id+' · '+(candidate.display_name||'Spool'), 'button',()=>{
      setValue('confirm-spool-id',candidate.id);
      list.querySelectorAll('button').forEach(b=>b.setAttribute('aria-pressed',String(b===button)));
      form.querySelector('[type=submit]').focus();
    });
    button.setAttribute('aria-pressed','false');list.append(button);
  });
}
function renderCurrentSpool() {
  if (!byId('current-spool')) return;
  const {t,w,present,spool}=spoolIdentity(), s=spool||{};
  const lifecycle=present&&String(w.tag?.uid||'')===String(t.uid||t.inventory?.uid||'')?w.tag_lifecycle:t.lifecycle;
  const blank=present&&(t.blank_compatible||lifecycle==='BLANK_COMPATIBLE'||(lifecycle==='READY'&&state.clearSnapshot?.phase==='cleared'));
  const linked=present&&!blank&&!!s.id, valid=present&&!blank&&(t.decode==='pass'||linked||lifecycle==='OPENPRINTTAG_UNLINKED');
  const cleanup=lifecycle==='CLEANUP_PENDING'||state.clearSnapshot?.phase==='unlink_pending';
  const writePending=lifecycle==='WRITE_PENDING';
  byId('tag-update').hidden=!linked||cleanup||writePending;
  byId('writer-open').hidden=(!blank&&!valid&&!writePending)||cleanup;
  byId('clear-open').hidden=(!valid&&!cleanup)||writePending;
  setText('writer-open',writePending?'Resume tag workflow':blank?'Assign tag to a spool':linked?'Reassign':'Link to a spool');
  setText('clear-open',cleanup?'Retry cleanup':'Clear / Reuse');
  byId('spool-empty').hidden=present;
  byId('current-spool').hidden=!present;
  if (!present) return;
  const remaining=first(s.remaining_grams,t.remaining_weight);
  const initial=first(s.initial_grams,t.actual_full_weight,t.nominal_full_weight);
  const name=first(s.display_name,t.material_name,t.blank_compatible?'Blank tag':'Reading spool…');
  setText('current-name',name);setText('manage-material',name);
  setText('current-vendor',first(s.vendor,t.brand_name,t.brand,''));
  setText('current-material',first(s.material,t.material_abbreviation,''));
  setText('current-number',blank?'Ready to assign':s.id?'Spool #'+s.id:'Not yet linked');
  setText('current-remaining',remaining===null?'—':Math.round(Number(remaining)));
  const percentage=remaining!==null&&Number(initial)>0?Math.max(0,Math.min(100,Math.round(Number(remaining)/Number(initial)*100))):null;
  byId('remaining-summary').hidden=percentage===null;
  if(percentage!==null){byId('remaining-meter').value=percentage;setText('remaining-caption',percentage+'% of '+Math.round(initial)+' g');}
  const c=s.primary_color||t.primary_color||t.color;
  const color=Array.isArray(c)?c.slice(0,3):c&&typeof c==='object'?[c.red,c.green,c.blue]:null;
  const safe=color?.length===3&&color.every(n=>Number.isInteger(n)&&n>=0&&n<=255);
  byId('current-spool').style.setProperty('--filament',safe?'rgb('+color.join(',')+')':'#879b9b');
  setText('current-color',safe?'Filament color':'Color not provided');
  setText('current-tag',t.blank_compatible?'Blank tag':t.decode==='pass'?'Tag valid':t.decode==='fail'?'Tag needs attention':'Reading tag…');
  byId('current-tag').dataset.valid=String(t.decode==='pass');
  setText('current-link',s.id?'Linked to Spoolman':w.stage==='resolving_spool'?'Looking up spool…':'Not linked to Spoolman');
  const assignments=state.printers.flatMap(p=>asArray(p.toolheads).filter(h=>s.id&&Number(first(h.assigned_spool_id,h.assigned_spool,h.spool_id))===Number(s.id)).map(h=>(p.display_name||p.name||'Printer')+' · T'+(Number(first(h.backend_id,h.id))+1)));
  setText('current-assignment',assignments.length?assignments.join(', '):'Not assigned');
  setText('dashboard-printer',assignments.length?assignments.join(', '):'Choose a toolhead to assign this spool.');
  const v=state.weighSync||{}, sameWeight=s.id&&Number(v.spool_id)===Number(s.id);
  setText('dashboard-weight',sameWeight&&v.measured!=null?formatGrams(v.measured)+' measured · '+formatGrams(v.canonical_remaining)+' in Spoolman':'Weigh this spool to compare it with your inventory.');
  setText('home-action-label',blank?'Assign tag':state.scale?.measurement?.active?'Weighing…':'Weigh');
  if(blank)byId('home-weigh').disabled=!!state.maintenance;
  byId('spool-edit').disabled=!s.id;byId('tag-update').disabled=!s.id;
  byId('spool-assign').disabled=!s.id||state.maintenance;
}
function presentWriter() {
  const d=byId('writer-dialog'),content=byId('writer-content');
  byId('writer-panel').hidden=false;
  if(content&&content.parentElement!==d)d.append(content);
  if(byId('manage-dialog')?.open){window.OpenTagWriter.writerState.returnManage=true;closeProductDialog('manage-dialog');}
  if(!d.open)d.showModal();
  document.body.classList.add('modal-open');
}
async function mountInventory() {
  const W=window.OpenTagWriter, d=byId('writer-dialog');
  if(!W?.writerState||!d||d.open||W.writerState.busy)return;
  try {
    const pending=await api('/tag-writer',{priority:PRIORITY.CONTROL});
    if(state.currentPage!=='inventory')return;
    if(pending.mode==='clear'&&['unlink_pending','clear_recovery','clearing','verifying','unlinking'].includes(pending.phase)){await openClear();return;}
    if(['association_pending','validating','writing','verifying','decoding','associating'].includes(pending.phase)){presentWriter();W.renderWriter(pending);return;}
  } catch(error) {showToast('Inventory unavailable. '+error.message+' Open Inventory again to retry.',true);return;}
  byId('inventory-content').replaceChildren(byId('writer-content'));
  W.writerState.step=1;
  W.selected(W.writerState.selected,W.writerState.material);
  await W.writerSearch('refresh');
}
async function currentSpoolTask(edit=false) {
  const s=spoolIdentity().spool,W=window.OpenTagWriter;
  if(!s?.id||!W||W.writerState.busy)return;
  presentWriter();
  // Fetches authoritative data and a fresh exact-tag preview; never writes here.
  if(await W.writerCommand({action:'preview',spool_id:s.id,mode:'rewrite'})){
    if(edit)W.openEditor('spool');
  }
}
function productElement(tag,text,cls) {
  const n=document.createElement(tag);if(text!=null)n.textContent=text;if(cls)n.className=cls;return n;
}
function bindInventoryFilters() {
  const group=productElement('details',null,'inventory-refine');
  group.append(productElement('summary','Refine this page'));
  const row=productElement('div',null,'field-grid'),color=productElement('select'),status=productElement('select');
  color.id='inventory-color';status.id='inventory-status';
  for(const [node,title] of [[color,'Color'],[status,'Status']]){const label=productElement('label',title);label.append(node);row.append(label);}
  for(const [value,title] of [['','All statuses'],['available','Filament remaining'],['empty','Empty'],['archived','Archived']]){const o=productElement('option',title);o.value=value;status.append(o);}
  const note=productElement('p',null,'hint');group.append(row,note);
  byId('writer-inventory').querySelector('.writer-toolbar').after(group);
  const apply=()=>{
    const S=window.OpenTagWriter.writerState;let count=0;
    S.rows?.forEach((row,i)=>{const s=S.items[i],f=S.entity==='spool'?asObject(s.filament):s;
      const matches=(!color.value||f.color_hex===color.value)&&(!status.value||S.entity!=='spool'||(status.value==='archived'?s.archived:status.value==='empty'?Number(s.remaining_weight)===0:!s.archived&&Number(s.remaining_weight)>0));
      row.hidden=!matches;if(matches)count++;
    });
    note.textContent=count+' visible on this page. Search and vendor/material filters apply across inventory.';
  };
  window.OpenTagInventoryFilter=()=>{
    const S=window.OpenTagWriter.writerState,previous=color.value;
    color.replaceChildren(productElement('option','All colors'));color.firstChild.value='';
    const colors=new Set(S.items.map(s=>(S.entity==='spool'?asObject(s.filament):s).color_hex).filter(Boolean));
    for(const value of colors){const o=productElement('option','#'+value);o.value=value;color.append(o);}
    if(colors.has(previous))color.value=previous;
    status.disabled=S.entity!=='spool';group.hidden=S.entity==='vendor';apply();
  };
  color.addEventListener('change',apply);status.addEventListener('change',apply);
}
function openAssignment() {
  const s=spoolIdentity().spool;
  if(!s?.id)return;
  const generation=state.spoolGeneration,spoolId=s.id;
  const dialog=byId('assign-dialog'),list=byId('assign-tools'),submit=byId('assign-confirm');
  list.replaceChildren();submit.disabled=true;dialog.dataset.busy='false';
  setText('assign-title','Assign '+(s.display_name||'spool #'+s.id));
  setText('assign-message','Choose a toolhead.');
  let choice=null;
  state.printers.forEach(printer=>{
    list.append(productElement('h3',printer.display_name||printer.name||'Printer'));
    const grid=productElement('div',null,'toolhead-grid');
    asArray(printer.toolheads).forEach(tool=>{
      const id=Number(first(tool.backend_id,tool.id)),mapped=first(tool.assigned_spool_id,tool.assigned_spool,tool.spool_id);
      const b=makeButton('', 'tool-choice',()=>{
        choice={printer:structuredClone(printer),tool:structuredClone(tool),revision:first(printer.revision,printer.printer_revision,state.printerRevision)};
        list.querySelectorAll('button').forEach(n=>n.setAttribute('aria-pressed',String(n===b)));
        setText('assign-message',mapped===null?'T'+(id+1)+' selected':'T'+(id+1)+' currently holds spool #'+mapped+'. You will be asked to confirm replacement.');
        submit.textContent='Assign to T'+(id+1);submit.disabled=choice.revision===null;
      },!Number.isInteger(id));
      b.setAttribute('aria-pressed','false');b.append(productElement('strong','T'+(id+1)),productElement('span',mapped===null?'Empty':'Spool #'+mapped));grid.append(b);
    });list.append(grid);
  });
  if(!list.children.length)setText('assign-message','Printer unavailable. Check FilaBridge in Settings, then try again. Nothing has been assigned.');
  submit.onclick=async()=>{
    if(!choice||dialog.dataset.busy==='true')return;
    if(state.spoolGeneration!==generation||spoolIdentity().spool?.id!==spoolId){setText('assign-message','The spool changed. Close this dialog and select the current spool again. Nothing was assigned.');submit.disabled=true;return;}
    dialog.dataset.busy='true';submit.disabled=true;
    setText('assign-message','Assigning to T'+(Number(first(choice.tool.backend_id,choice.tool.id))+1)+'…');
    const success=await assignToolhead(choice.printer,choice.tool,choice.revision);
    dialog.dataset.busy='false';
    setText('assign-message',success?'Assigned and verified on the printer.':'Assignment was not verified. Check the station message and refresh the printer before trying again.');
    setText('assign-confirm',success?'Assigned':'Close and retry');
  };
  openProductDialog('assign-dialog');
}
function selectSettings(name) {
  document.querySelectorAll('[data-settings-pane]').forEach(n=>n.hidden=n.dataset.settingsPane!==name);
  document.querySelectorAll('[data-setting]').forEach(n=>n.setAttribute('aria-current',n.dataset.setting===name?'page':'false'));
}
function buildSettings() {
  const root=byId('settings'),form=byId('config-form');
  if(!form)return;
  const panes={};
  for(const name of ['station','integrations','scale','network','display','advanced']){
    const pane=productElement('div',null,'settings-pane');pane.dataset.settingsPane=name;panes[name]=pane;root.append(pane);
  }
  const config=byId('configuration');config.hidden=false;panes.station.append(config);
  const fields=[...form.querySelectorAll('fieldset')];
  fields.forEach(field=>{
    const title=field.querySelector('legend')?.textContent||'';
    const name=/Spoolman|FilaBridge/.test(title)?'integrations':/Load-cell/.test(title)?'scale':/Wi-Fi/.test(title)?'network':/Toolhead|security/.test(title)?'advanced':'station';
    const group=productElement('details',null,'settings-edit');group.append(productElement('summary','Edit '+title),field);panes[name].append(group);
    // Preserve the single optimistic-concurrency form even though fields are grouped visually.
    field.querySelectorAll('input,select,button').forEach(n=>n.setAttribute('form','config-form'));
  });
  const grid=root.querySelector('.settings-grid');
  if(grid){[...grid.children].forEach((n,i)=>panes[['network','integrations','scale','advanced'][i]||'advanced'].prepend(n));grid.remove();}
  setText('configuration-title','Your station');
  panes.advanced.append(byId('config-revision'),config.querySelector('.transfer-card'));
  const deviceGroup=panes.station.querySelector('.settings-edit');if(deviceGroup)deviceGroup.open=true;
  const policy=productElement('fieldset',null,'card');policy.disabled=true;policy.append(productElement('legend','Weight updates'));
  const automatic=byId('config-auto-weigh').parentElement,hint=automatic.nextElementSibling;
  policy.append(automatic,hint,root.querySelector('label[for="config-weight-tolerance"]'),byId('config-weight-tolerance'));
  panes.scale.insertBefore(policy,panes.scale.querySelector('.settings-edit'));
  const networkLink=panes.network.querySelector('a[href="#configuration"]');
  networkLink?.addEventListener('click',e=>{e.preventDefault();panes.network.querySelector('.settings-edit').open=true;byId('config-ssid').focus();});
  const display=productElement('div');display.append(productElement('h3','Touchscreen display'),productElement('p','Adjust brightness and sleep on the station touchscreen.','muted'));panes.display.append(display);
  const calibration=makeButton('Recalibrate scale','button',()=>{openProductDialog('weigh-dialog');setCalibrationPanel(true);});panes.scale.append(calibration);
  for(const id of ['diagnostics','maintenance','spool-diagnostics']){const n=byId(id);if(n){n.hidden=false;panes.advanced.append(n);}}
  const save=byId('config-save').parentElement;root.append(save);
  byId('config-save').setAttribute('form','config-form');
  root.addEventListener('input',markConfigDirty);
  root.addEventListener('change',markConfigDirty);
  // Dynamically created profile fields remain part of the optimistic settings form.
  const station=fields.find(f=>f.querySelector('#config-brightness'));
  if(station){const brightness=byId('config-brightness'),label=station.querySelector('label[for="config-brightness"]');panes.display.replaceChildren(productElement('h3','Touchscreen display'),productElement('p','Set a comfortable brightness for the station.','muted'),label,brightness);}
  selectSettings('station');
}
function bindProduct() {
  if(byId('overview').dataset.bound)return;byId('overview').dataset.bound='true';
  document.querySelectorAll('[data-close]').forEach(b=>b.addEventListener('click',()=>closeProductDialog(b.dataset.close)));
  ['weigh-dialog','manage-dialog','assign-dialog'].forEach(id=>byId(id).addEventListener('cancel',e=>{e.preventDefault();closeProductDialog(id);}));
  byId('new-tag').addEventListener('click',()=>{window.OpenTagWriter.writerState.opener=byId('new-tag');window.OpenTagWriter.openModal();});
  byId('spool-manage').addEventListener('click',()=>openProductDialog('manage-dialog'));
  byId('spool-details').addEventListener('click',()=>openProductDialog('manage-dialog'));
  byId('spool-assign').addEventListener('click',openAssignment);
  byId('spool-edit').addEventListener('click',()=>currentSpoolTask(true));
  byId('tag-update').addEventListener('click',()=>currentSpoolTask());
  byId('inventory-community').addEventListener('click',async()=>{await window.OpenTagWriter.openModal();setValue('writer-source','community');byId('writer-source').dispatchEvent(new Event('change'));});
  document.querySelectorAll('[data-setting]').forEach(n=>n.addEventListener('click',()=>selectSettings(n.dataset.setting)));
  buildSettings();bindInventoryFilters();renderCurrentSpool();
  const tagAdvanced=byId('nfc').querySelector('details .facts');
  ['nfc-reader-state','nfc-technology','nfc-identity'].forEach(id=>{const row=byId(id)?.closest('.facts>div');if(row&&tagAdvanced)tagAdvanced.append(row);});
  byId('nfc-read-status').classList.add('visually-hidden');
  const hardware=productElement('details');hardware.id='scale-hardware';hardware.append(productElement('summary','Scale setup'));
  for(const id of ['tare-scale','calibrate-scale','calibration-panel','scale-action-status'])hardware.append(byId(id));
  byId('weigh-scale').parentElement.append(hardware);
}
