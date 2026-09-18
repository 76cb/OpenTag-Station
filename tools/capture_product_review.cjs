// Playwright is a review-only dependency, never shipped on the station.
const {chromium}=require('playwright');
const fs=require('fs'),path=require('path'),http=require('http');
const crypto=require('crypto'),captures={};
const journeys=require('./product_journeys.cjs');
const root=path.resolve(process.argv[2]||'.pio/ui-review');
const server=http.createServer((req,res)=>{const name=path.basename(new URL(req.url,'http://localhost').pathname)||'index.html';const file=path.join(root,name);if(!fs.existsSync(file)){res.writeHead(404);return res.end();}res.setHeader('Content-Type',name.endsWith('.js')?'application/javascript':name.endsWith('.css')?'text/css':'text/html');res.end(fs.readFileSync(file));});
(async()=>{await new Promise(r=>server.listen(0,'127.0.0.1',r));const browser=await chromium.launch({headless:true,...(process.env.CHROME_BIN?{executablePath:process.env.CHROME_BIN}:{})});
try {for(const width of [1440,1280,1024,768,390]){for(const scene of ['empty','dashboard','weigh','assignment','manage','inventory','printer','writer','preview','settings','clear','blank','unlinked','reuse',...(width===1440||width===390?['settings-station','settings-scale','settings-network','settings-display','settings-advanced']:[])]){
const page=await browser.newPage({viewport:{width,height:width===390?844:1000}});const errors=[];page.on('pageerror',e=>errors.push(e.message));
if(scene==='writer')page.on('dialog',d=>d.accept());
await page.goto(`http://127.0.0.1:${server.address().port}/index.html?scene=${scene}`);await page.waitForFunction(()=>document.documentElement.dataset.review,{timeout:10000});
const result=await page.evaluate(()=>({state:document.documentElement.dataset.review,overflow:document.documentElement.scrollWidth>innerWidth,error:document.querySelector('#fixture-status').textContent}));
if(errors.length||result.state!=='ready'||result.overflow)throw Error(`${width} ${scene}: ${JSON.stringify({errors,...result})}`);
const publicText=await page.evaluate(()=>document.body.innerText+'\n'+document.documentElement.outerHTML);
if(publicText.toLowerCase().includes('ca'+'sy'))throw Error('Public screenshot privacy check failed');
await page.screenshot({path:path.join(root,`${width}-${scene}.png`),fullPage:false});
captures[`${width}-${scene}.png`]=crypto.createHash('sha256').update(fs.readFileSync(path.join(root,`${width}-${scene}.png`))).digest('hex');
if(width===1440||width===390)await journeys(page,scene,width);
if(errors.length)throw Error(errors.join('\n'));
await page.close();console.log(`${width} ${scene} captured`);
}}fs.writeFileSync(path.join(root,'capture-provenance.json'),JSON.stringify({privacy_checked_before_capture:true,images:captures},null,2)+'\n');}finally{await browser.close();server.close();}})().catch(e=>{console.error(e);server.close();process.exitCode=1;});
