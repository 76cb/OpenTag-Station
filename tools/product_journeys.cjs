const assert=require('node:assert/strict');
module.exports=async function productJourneys(page,scene,width){
  const visible=id=>page.locator('#'+id).isVisible();
  if(scene==='empty'){
    assert.equal(await visible('spool-empty'),true);assert.equal(await visible('current-spool'),false);
    assert.deepEqual(await page.locator('[data-nav]').allTextContents(),['Dashboard','Inventory','Printer','Settings']);
    assert.equal(await page.locator('#content').innerText().then(t=>/Spoolman ID|Reconciliation/.test(t)),false);
    await page.locator('#new-tag').click();assert.equal(await visible('writer-dialog'),true);
  }
  if(scene==='dashboard'){
    assert.equal(await page.locator('#current-remaining').textContent(),'712');
    assert.match(await page.locator('#remaining-caption').textContent(),/71%/);
    if(width===390){for(const id of ['home-weigh','spool-assign','spool-manage']){const box=await page.locator('#'+id).boundingBox();assert.ok(box.y+box.height<=770,id+' stays above bottom navigation');}}
    await page.evaluate(()=>{const a=window.__OpenTagTest;delete a.state.spool.initial_grams;delete a.state.currentTag.actual_full_weight;delete a.state.currentTag.nominal_full_weight;a.renderCurrentSpool();});
    assert.equal(await visible('remaining-summary'),false);
    await page.evaluate(()=>window.__OpenTagTest.renderNfc({present:false,inventory:{present:false}}));
    assert.equal(await visible('current-spool'),false);
  }
  if(scene==='manage'){
    const text=await page.locator('#nfc').innerText();assert.ok(!text.includes('NFC-V / ISO15693'));
    assert.equal(await visible('tag-update'),true);
    await page.locator('#tag-update').click();await page.waitForFunction(()=>document.getElementById('writer-review').hidden===false);
    assert.equal(await visible('writer-inventory'),false);assert.match(await page.locator('#writer-review-title').textContent(),/Ready to write/);
    assert.equal(await page.evaluate(()=>requests.filter(r=>r.action==='write').length),0,'update opens a fresh preview without writing');
  }
  if(scene==='settings'){
    assert.equal(await visible('config-spoolman-token'),false);assert.equal(await visible('diagnostics'),false);
    await page.getByText('Edit Spoolman',{exact:true}).click();await page.locator('#config-spoolman-url').fill('http://spoolman.example:7912');
    assert.equal(await page.evaluate(()=>window.__OpenTagTest.state.configDirty),true,'grouped fields preserve dirty tracking');
    await page.locator('[data-setting="advanced"]').click();assert.equal(await visible('diagnostics'),true);
  }
  if(scene==='inventory'){
    await page.locator('.inventory-refine summary').click();
    await page.locator('#inventory-status').selectOption('empty');
    assert.equal(await page.locator('#writer-results button:visible').count(),1);
    await page.locator('#inventory-status').selectOption('');
    await page.locator('.inventory-refine summary').click();
    await page.locator('#writer-results button').nth(1).click();await page.locator('#writer-continue').click();
    assert.equal(await visible('writer-edit-spool'),true);await page.locator('#writer-preview').click();
    await page.waitForFunction(()=>document.getElementById('writer-dialog').open&&window.OpenTagWriter.writerState.snapshot.phase==='preview');
    assert.equal(await visible('writer-confirm'),true);
    await page.locator('#writer-close').click();
    await page.waitForFunction(()=>document.getElementById('writer-content').parentElement.id==='inventory-content'&&!window.OpenTagWriter.writerState.busy);
    assert.equal(await visible('writer-results'),true,'closing preview restores the inventory browser');
  }
  if(scene==='assignment'){
    const confirmations=[];page.on('dialog',async d=>{confirmations.push(d.message());await d.accept();});
    await page.evaluate(()=>{window.assignmentRequests=[];const original=window.fixtureReply;window.fixtureReply=async(path,options)=>{
      if(String(path).includes('/toolheads/')&&options.method==='POST'){window.assignmentRequests.push(JSON.parse(options.body));return new Response(JSON.stringify({api_version:'v1',ok:true,data:{operation_id:42}}),{status:202,headers:{'Content-Type':'application/json'}});}
      return original(path,options);
    };});
    await page.locator('#assign-tools button').nth(1).click();await page.locator('#assign-confirm').click();
    await page.waitForFunction(()=>document.getElementById('assign-dialog').dataset.busy==='false');
    assert.match(await page.locator('#assign-message').textContent(),/Assigned and verified/,await page.locator('#toast').textContent());
    assert.equal(confirmations.length,1);assert.match(confirmations[0],/occupied/);
    const requests=await page.evaluate(()=>window.assignmentRequests);assert.equal(requests.length,1);
    assert.equal(requests[0].expected_current_spool_id,27);assert.equal(requests[0].expected_spool_id,28);assert.equal(requests[0].spool_generation,3);assert.equal(requests[0].printer_revision,7);assert.equal(requests[0].replace_occupied_confirmed,true);
  }
  if(scene==='writer'){
    await page.locator('[data-writer-source="community"]').click();await page.getByRole('button',{name:/Community PLA/}).click();
    await page.locator('#writer-import').click();await page.waitForFunction(()=>window.OpenTagWriter.writerState.filament===88&&!window.OpenTagWriter.writerState.busy);
    assert.equal(await page.locator('#writer-source').inputValue(),'spoolman');
    await page.locator('#writer-create-submit').click();await page.waitForFunction(()=>window.OpenTagWriter.writerState.spool===29);
    await page.locator('#writer-preview').click();await page.waitForFunction(()=>window.OpenTagWriter.writerState.snapshot.phase==='preview');
    assert.equal(await page.evaluate(()=>window.OpenTagWriter.writerState.snapshot.spool_id),29);
  }
  console.log(`Product journey passed: ${width} ${scene}`);
};
