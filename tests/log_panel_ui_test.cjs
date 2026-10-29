const {componentsRoot}=require('./component_paths.cjs');
/* Real embedded-page logging lifecycle across all supported frontend types. */
const assert=require('node:assert/strict');
const http=require('node:http');
const fs=require('node:fs');
const path=require('node:path');
const {execFileSync}=require('node:child_process');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'..');
const html=execFileSync(process.env.TZB_PYTHON_BIN||'python3',[path.join(componentsRoot,'tubeszb_web/assets.py')],{cwd:root,encoding:'utf8'});
let device={};let connections=0;const streams=new Set();
const network={nonce:'network-token',mode:'dhcp',ip:'192.168.1.55',gateway:'192.168.1.1',subnet:'255.255.255.0',
 dns1:'192.168.1.1',dns2:'0.0.0.0',hostname:'tubeszb.local',storage_ok:true,testing:false,restarting:false};
const server=http.createServer((req,res)=>{
 const url=new URL(req.url,'http://127.0.0.1');res.setHeader('Cache-Control','no-store');
 if(url.pathname==='/events'){
  connections++;res.writeHead(200,{'Content-Type':'text/event-stream','Connection':'keep-alive'});
  streams.add(res);res.on('close',()=>streams.delete(res));res.write(': connected\n\n');return;
 }
 if(url.pathname==='/tubeszb/status'){res.setHeader('Content-Type','application/json');res.end(JSON.stringify(device));return;}
 if(url.pathname==='/network/status'){res.setHeader('Content-Type','application/json');res.end(JSON.stringify(network));return;}
 res.setHeader('Content-Type','text/html');res.end(html);
});
function emit(text){for(const stream of streams)stream.write('event: log\ndata: '+text+'\n\n');}
async function until(condition){for(let i=0;i<500;i++){if(condition())return;await new Promise(r=>setTimeout(r,20));}assert.ok(condition());}
(async()=>{
 await new Promise(r=>server.listen(0,'127.0.0.1',r));
 const browser=await chromium.launch({headless:true,executablePath:process.env.TZB_CHROMIUM_PATH||undefined});
 try{
  for(const type of ['zwave','cc2652','efr32']){
   device={nonce:'log-token',name:'tubeszb-'+type,model:'TubesZB '+type,radio_type:type,project_version:'2026.10.25.1',esphome_version:'2026.10.1',
    mac:'24:6F:28:A1:7C:55',board:'esp-wrover-kit',ethernet_connected:true,api_connected:true,proxy_connected:true,baud_rate:115200,
    home_id:0,uptime:1000,free_heap:140000,update_state:1,latest_version:'2026.10.25.1',update_error:false,checking:false,action_pending:false,
    zigbee:{busy:false,connected:true,paused:false,firmware:'20250321',latest:'20250321',board_name:type==='cc2652'?'CC2652P2':'BM24',
     manufacturer:'TubesZB',ieee:'00124b0000000001',info_status:'Ready',custom_url:'',baud:'115200',baud_options:['115200'],
     gateway_beta:false,zigbee_beta:false,checked:false,operation:'',result:0,progress:-1,port:6638}};
   const page=await browser.newPage({viewport:{width:1100,height:1050},acceptDownloads:true});const errors=[];
   page.on('pageerror',e=>errors.push(e.message));
   await page.goto('http://127.0.0.1:'+server.address().port+'/?view=overview');
   await page.getByText('Device online',{exact:true}).waitFor();
   const panel=page.locator('[data-ui="log-panel"]');
   assert.equal(await panel.isVisible(),false);assert.equal(streams.size,0);
   const before=connections;
   await page.getByRole('button',{name:'Firmware',exact:true}).click();
   assert.equal(await panel.isVisible(),true);assert.equal(await panel.getAttribute('open'),null);assert.equal(connections,before);
   await panel.locator('summary').click();await page.getByRole('button',{name:'Stop capture',exact:true}).waitFor();await until(()=>streams.size===1);
   emit('first '+type+' log');
   await page.waitForFunction(type=>document.querySelector('[data-ui="logs"]').textContent.includes('first '+type+' log'),type);
   const views=type==='zwave'?['Network','Diagnostics','Firmware']:['Zigbee','Network','Diagnostics','Firmware'];
   for(const view of views){
    await page.getByRole('button',{name:view,exact:true}).click();
    assert.equal(await panel.isVisible(),true);assert.equal(await panel.getAttribute('open'),'');
    emit('during '+view+' '+type);
    await page.waitForFunction(text=>document.querySelector('[data-ui="logs"]').textContent.includes(text),'during '+view+' '+type);
    assert.equal(connections,before+1,'Section changes must reuse one EventSource');assert.equal(streams.size,1);
   }
   await panel.locator('summary').click();assert.equal(await panel.getAttribute('open'),null);
   emit('while collapsed '+type);
   await page.waitForFunction(text=>document.querySelector('[data-ui="logs"]').textContent.includes(text),'while collapsed '+type);
   await panel.locator('summary').click();assert.equal(connections,before+1);
   await page.getByRole('button',{name:'Overview',exact:true}).click();assert.equal(await panel.isVisible(),false);
   emit('while Overview '+type);
   await page.getByRole('button',{name:'Diagnostics',exact:true}).click();
   await page.waitForFunction(text=>document.querySelector('[data-ui="logs"]').textContent.includes(text),'while Overview '+type);
   assert.equal(connections,before+1);
   if(process.env.TZB_SCREENSHOT_DIR&&type==='cc2652'){
    fs.mkdirSync(process.env.TZB_SCREENSHOT_DIR,{recursive:true});
    await page.getByRole('button',{name:'Firmware',exact:true}).click();
    await page.screenshot({path:path.join(process.env.TZB_SCREENSHOT_DIR,'logs-firmware-desktop.png'),fullPage:true});
   }
   await page.getByRole('button',{name:'Stop capture',exact:true}).click();await until(()=>streams.size===0);
   await page.getByRole('button',{name:'Network',exact:true}).click();
   assert.equal(await page.getByRole('button',{name:'Start capture',exact:true}).isVisible(),true);
   assert.equal(await page.getByRole('button',{name:'Download logs',exact:true}).isEnabled(),true);
   assert.equal(connections,before+1,'Navigation must not silently restart a stopped capture');
   const downloadPromise=page.waitForEvent('download');await page.getByRole('button',{name:'Download logs',exact:true}).click();
   const download=await downloadPromise;const log=fs.readFileSync(await download.path(),'utf8');
   assert.match(log,new RegExp('first '+type+' log'));assert.match(log,new RegExp('while Overview '+type));
   await page.getByRole('button',{name:'Start capture',exact:true}).click();await until(()=>streams.size===1);
   emit('after resume '+type);
   await page.waitForFunction(text=>document.querySelector('[data-ui="logs"]').textContent.includes(text),'after resume '+type);
   assert.equal(connections,before+2);assert.equal(await page.locator('[data-ui="logs"]').textContent().then(t=>t.includes('first '+type+' log')),true);
   // Retry the same EventSource after an interrupted connection; keep the buffer.
   for(const stream of [...streams])stream.end();await until(()=>streams.size===0);
   await until(()=>streams.size===1);emit('after reconnect '+type);
   await page.waitForFunction(text=>document.querySelector('[data-ui="logs"]').textContent.includes(text),'after reconnect '+type);
   assert.equal(await page.locator('[data-ui="logs"]').textContent().then(t=>t.includes('first '+type+' log')),true);
   for(const width of [390,320]){
    await page.setViewportSize({width,height:900});
    for(const view of views){await page.getByRole('button',{name:view,exact:true}).click();assert.equal(await page.locator('#tzb-app').evaluate(el=>el.scrollWidth>el.clientWidth),false,view+' logs overflow at '+width);}
   }
   if(process.env.TZB_SCREENSHOT_DIR&&type==='cc2652')await page.screenshot({path:path.join(process.env.TZB_SCREENSHOT_DIR,'logs-firmware-mobile.png'),fullPage:true});
   await page.getByRole('button',{name:'Clear',exact:true}).click();
   assert.equal(await page.getByRole('button',{name:'Download logs',exact:true}).isDisabled(),true);
   emit('new after clear '+type);await page.waitForFunction(text=>document.querySelector('[data-ui="logs"]').textContent.includes(text),'new after clear '+type);
   await page.close();await until(()=>streams.size===0);assert.deepEqual(errors,[]);
  }
  console.log('Shared log dropdown checks passed for Z-Wave, CC2652 and EFR32: all non-Overview sections, one stream across navigation/collapse, retained text/downloads, explicit stop/resume, reconnect, bounds/mobile layout and page-close cleanup.');
 }finally{await browser.close();for(const stream of streams)stream.end();await new Promise(r=>server.close(r));}
})().catch(error=>{console.error(error);process.exitCode=1;for(const stream of streams)stream.end();server.close();});
