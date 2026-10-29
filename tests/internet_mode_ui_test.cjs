/* Real embedded-page logging lifecycle across all supported frontend types. */
const assert=require('node:assert/strict');
const http=require('node:http');
const fs=require('node:fs');
const path=require('node:path');
const {execFileSync}=require('node:child_process');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'..');
const html=execFileSync(process.env.TZB_PYTHON_BIN||'python3',['components/tubeszb_web/assets.py'],{cwd:root,encoding:'utf8'});
let device={};let saves=0;let rejectSave=false;let connections=0;const streams=new Set();
const network={nonce:'network-token',mode:'dhcp',ip:'192.168.1.55',gateway:'192.168.1.1',subnet:'255.255.255.0',
 dns1:'192.168.1.1',dns2:'0.0.0.0',hostname:'tubeszb.local',storage_ok:true,testing:false,restarting:false};
const server=http.createServer((req,res)=>{
 const url=new URL(req.url,'http://127.0.0.1');res.setHeader('Cache-Control','no-store');
 if(url.pathname==='/events'){
  connections++;res.writeHead(200,{'Content-Type':'text/event-stream','Connection':'keep-alive'});
  streams.add(res);res.on('close',()=>streams.delete(res));res.write(': connected\n\n');return;
 }
 if(url.pathname==='/network/internet'){
  assert.equal(req.method,'POST');assert.equal(req.headers['x-tubeszb-network'],'network-token');
  let body='';req.on('data',chunk=>body+=chunk);req.on('end',()=>{
   res.setHeader('Content-Type','application/json');
   if(rejectSave){res.writeHead(409);res.end(JSON.stringify({error:'Could not save internet use.'}));return;}
   network.use_without_internet=new URLSearchParams(body).get('use_without_internet')==='true';saves++;res.end('{"ok":true}');
  });return;
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

   device.update_state=2;device.latest_version='2026.11.0';device.update_verified=true;
   network.use_without_internet=false;network.testing=false;
   const page=await browser.newPage({viewport:{width:1100,height:1000}});const errors=[];page.on('pageerror',e=>errors.push(e.message));
   await page.goto('http://127.0.0.1:'+server.address().port+'/?view=network');await page.getByText('Device online',{exact:true}).waitFor();
   const toggle=page.getByRole('switch',{name:'Use without internet',exact:true});
   assert.equal(await toggle.isChecked(),false);const before=saves;
   await toggle.click();await page.getByRole('dialog').waitFor();
   assert.match(await page.locator('[data-ui="dialog-copy"]').textContent(),/prevents it from checking for new firmware/);
   const release='https://github.com/tube0013/TubesZB-ESPHome-Builder/releases';
   assert.equal(await page.getByRole('dialog').getByRole('link').getAttribute('href'),release);
   await page.getByRole('button',{name:'Cancel',exact:true}).click();assert.equal(saves,before);assert.equal(await toggle.isChecked(),false);
   rejectSave=true;await toggle.click();await page.getByRole('button',{name:'Use without internet',exact:true}).click();
   await page.getByText('Could not save internet use.',{exact:true}).waitFor();assert.equal(await toggle.isChecked(),false);assert.equal(saves,before);
   rejectSave=false;await toggle.click();await page.getByRole('button',{name:'Use without internet',exact:true}).click();
   await page.getByText('Use without internet enabled and saved.',{exact:true}).waitFor();assert.equal(saves,before+1);assert.equal(await toggle.isChecked(),true);
   await page.getByRole('button',{name:'Firmware',exact:true}).click();
   assert.equal(await page.locator('[data-ui="offline-reminder"]').isVisible(),true);
   assert.equal(await page.locator('[data-ui="offline-reminder"] a').getAttribute('href'),release);
   assert.equal(await page.getByRole('button',{name:'Check for updates',exact:true}).isDisabled(),true);
   assert.equal(await page.locator('[data-ui="install-update"]').isVisible(),false);
   assert.equal(await page.getByRole('button',{name:'Choose file',exact:true}).isEnabled(),true);
   if(type!=='zwave'){
    assert.equal(await page.getByRole('button',{name:'Check Zigbee firmware',exact:true}).isDisabled(),true);
    assert.equal(await page.getByRole('button',{name:'Flash Zigbee firmware',exact:true}).isDisabled(),true);
    assert.equal(await page.getByRole('button',{name:'Trigger Bootloader Manually',exact:true}).isEnabled(),true);
   }
   // Reload retrieves the same persisted device setting, rather than localStorage.
   await page.reload();await page.locator('[data-ui="offline-reminder"]:not([hidden])').waitFor();
   await page.getByRole('button',{name:'Network',exact:true}).click();assert.equal(await toggle.isChecked(),true);
   if(process.env.TZB_SCREENSHOT_DIR&&type==='cc2652'){
    fs.mkdirSync(process.env.TZB_SCREENSHOT_DIR,{recursive:true});await page.emulateMedia({colorScheme:'dark'});
    await page.screenshot({path:path.join(process.env.TZB_SCREENSHOT_DIR,'offline-network-desktop.png'),fullPage:true});
   }
   for(const width of [390,320]){
    await page.setViewportSize({width,height:900});
    for(const view of ['Firmware','Network']){await page.getByRole('button',{name:view,exact:true}).click();assert.equal(await page.locator('#tzb-app').evaluate(el=>el.scrollWidth>el.clientWidth),false,view+' overflow at '+width);}
   }
   if(process.env.TZB_SCREENSHOT_DIR&&type==='cc2652')await page.screenshot({path:path.join(process.env.TZB_SCREENSHOT_DIR,'offline-network-mobile.png'),fullPage:true});
   await toggle.click();await page.getByText('Internet checks enabled and saved.',{exact:true}).waitFor();assert.equal(await toggle.isChecked(),false);
   await page.getByRole('button',{name:'Firmware',exact:true}).click();assert.equal(await page.locator('[data-ui="offline-reminder"]').isVisible(),false);
   assert.equal(await page.getByRole('button',{name:'Check for updates',exact:true}).isEnabled(),true);
   await page.close();assert.deepEqual(errors,[]);
  }
  console.log('Internet mode UI checks passed for Z-Wave, CC2652 and EFR32: confirm/cancel, save failure, persisted reload, release links, check/download guards, local OTA and controls, mobile layouts.');
 }finally{await browser.close();for(const stream of streams)stream.end();await new Promise(r=>server.close(r));}
})().catch(error=>{console.error(error);process.exitCode=1;for(const stream of streams)stream.end();server.close();});
