const {componentsRoot}=require('./component_paths.cjs');
const assert=require('node:assert/strict');
const http=require('node:http');
const fs=require('node:fs');
const path=require('node:path');
const {execFileSync}=require('node:child_process');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'..');
const html=execFileSync(process.env.TZB_PYTHON_BIN||'python3',[path.join(componentsRoot,'tubeszb_web/assets.py')],{cwd:root,encoding:'utf8'});
const device={nonce:'cc-token',name:'tubeszb-cc2652p2-poe-2021',model:'TubesZB CC2652P2 POE 2021',board:'esp-wrover-kit',radio_type:'cc2652',
 project_version:'2026.10.25.1',esphome_version:'2026.10.1',mac:'24:6F:28:A1:7C:52',ethernet_connected:true,api_connected:true,
 proxy_connected:true,baud_rate:115200,home_id:0,uptime:5000,free_heap:140000,update_state:2,latest_version:'old-source',
 update_error:false,checking:false,update_verified:false,action_pending:false,
 zigbee:{busy:false,connected:true,paused:false,firmware:'20250321',latest:'20250321',board_name:'CC2652P2',manufacturer:'Texas Instruments',
 ieee:'00124b0000000002',info_status:'Ready',custom_url:'',baud:'115200',baud_options:[],gateway_beta:false,zigbee_beta:false,
 checked:false,operation:'',result:0,progress:-1,port:6638}};
const network={mode:'static',testing:false,remaining:120,restarting:false,storage_ok:true,nonce:'network-token',ip:'192.168.1.52',
 subnet:'255.255.255.0',gateway:'192.168.1.1',dns1:'192.168.1.1',dns2:'0.0.0.0',return_ip:'192.168.1.52',hostname:'tubeszb-cc2652p2-poe-2021.local'};
const actions=[];
const server=http.createServer(async(req,res)=>{
 const url=new URL(req.url,'http://127.0.0.1');res.setHeader('Cache-Control','no-store');
 if(req.method==='GET'&&url.pathname==='/'){res.setHeader('Content-Type','text/html');res.end(html);return;}
 res.setHeader('Content-Type','application/json');
 if(req.method==='GET'&&url.pathname==='/tubeszb/status'){res.end(JSON.stringify(device));return;}
 if(req.method==='GET'&&url.pathname==='/network/status'){res.end(JSON.stringify(network));return;}
 let raw='';for await(const chunk of req)raw+=chunk;
 const body=Object.fromEntries(new URLSearchParams(raw));actions.push({path:url.pathname,body});
 assert.equal(req.headers['x-tubeszb-ui'],'cc-token');
 if(url.pathname==='/tubeszb/check-update'){device.update_verified=true;device.update_state=1;device.latest_version=device.project_version;}
 if(url.pathname==='/tubeszb/zigbee-action'){
  const radio=device.zigbee;radio.result=2;radio.operation='settings';
  switch(body.action){
   case 'gateway-channel':radio.gateway_beta=body.value==='beta';device.update_verified=false;device.update_state=2;break;
   case 'zigbee-channel':radio.zigbee_beta=body.value==='beta';radio.checked=false;break;
   case 'check':radio.operation='check';radio.checked=!radio.zigbee_beta;radio.result=radio.checked?2:3;break;
   case 'flash':radio.operation='flash';radio.result=1;radio.busy=true;radio.paused=true;radio.progress=55;break;
   case 'probe':radio.operation='probe';radio.info_status='CC2652 operation complete';break;
   case 'reset':radio.info_status='CC2652 settings applied';break;
   case 'bootloader':radio.operation='bootloader';radio.info_status='Bootloader ready; reset the radio to resume normal operation';break;
   case 'pause':radio.paused=true;break;
   case 'resume':radio.paused=false;break;
   default:assert.fail('Unexpected CC2652 action: '+body.action);
  }
 }
 res.end('{"ok":true}');
});
(async()=>{
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 const browser=await chromium.launch({headless:true,executablePath:process.env.TZB_CHROMIUM_PATH||undefined});
 try{
  const page=await browser.newPage({viewport:{width:1100,height:1200}});const errors=[];
  page.on('pageerror',error=>errors.push(error.message));
  await page.goto('http://127.0.0.1:'+server.address().port+'/?view=overview');
  await page.getByText('Zigbee client connected',{exact:true}).waitFor();
  assert.equal(await page.getByText('Connect ZHA or Zigbee2MQTT to tcp://192.168.1.52:6638.',{exact:true}).isVisible(),true);
  assert.equal(await page.getByText('esp-wrover-kit',{exact:true}).isVisible(),true);
  const ready=()=>page.waitForFunction(()=>!document.querySelector('[data-action="check-update"]').disabled);
  await page.getByRole('button',{name:'Firmware',exact:true}).click();
  assert.deepEqual(await page.locator('[data-view=firmware] h2:visible').allTextContents(),['TubesZB firmware','Install a firmware file','Zigbee radio firmware']);
  assert.equal(await page.getByText('Custom radio firmware source',{exact:true}).isVisible(),false);
  assert.equal(await page.getByRole('button',{name:'Flash Zigbee firmware',exact:true}).isDisabled(),true);
  await page.getByLabel('Use beta gateway firmware',{exact:true}).check();await ready();
  assert.equal(device.zigbee.zigbee_beta,false);
  assert.equal(await page.getByRole('button',{name:'Install update',exact:true}).isVisible(),false);
  await page.getByRole('button',{name:'Check for updates',exact:true}).click();
  await page.getByText('You’re up to date.',{exact:true}).waitFor();
  await page.getByLabel('Use beta Zigbee firmware',{exact:true}).check();await ready();
  await page.getByRole('button',{name:'Check Zigbee firmware',exact:true}).click();await ready();
  await page.getByText('Zigbee operation failed. Open Diagnostics for details.',{exact:true}).waitFor();
  assert.equal(await page.getByRole('button',{name:'Flash Zigbee firmware',exact:true}).isDisabled(),true,'Failed beta checks cannot unlock flashing');
  await page.getByLabel('Use beta Zigbee firmware',{exact:true}).uncheck();await ready();
  assert.equal(device.zigbee.gateway_beta,true);
  await page.getByRole('button',{name:'Check Zigbee firmware',exact:true}).click();await ready();
  await page.getByText('Selected firmware source checked.',{exact:true}).waitFor();
  const beforeFlash=actions.length;
  await page.getByRole('button',{name:'Flash Zigbee firmware',exact:true}).click();
  assert.equal(actions.length,beforeFlash);
  await page.getByRole('button',{name:'Flash Zigbee radio',exact:true}).click();
  await page.locator('[data-ui="radio-progress"]:not([hidden])').waitFor();
  await page.waitForFunction(()=>document.querySelector('[data-ui="radio-progress"]').value===55);
  assert.equal(await page.locator('[data-ui="radio-progress"]').getAttribute('value'),'55');
  assert.equal(await page.locator('[data-ui="radio-progress"]').evaluate(el=>Boolean(el.closest('[data-ui="toast"]'))),true,'Radio progress belongs in the top banner');
  assert.deepEqual(actions.slice(beforeFlash).map(x=>x.body.action),['flash'],'The browser must call the shared flash path once; UART ownership belongs to the device script');
  assert.equal(device.zigbee.paused,true,'Device flash path owns the paused bridge');
  assert.equal(await page.getByLabel('Use beta Zigbee firmware',{exact:true}).isDisabled(),true);
  if(process.env.TZB_SCREENSHOT_DIR){fs.mkdirSync(process.env.TZB_SCREENSHOT_DIR,{recursive:true});await page.screenshot({path:path.join(process.env.TZB_SCREENSHOT_DIR,'cc2652-firmware.png'),fullPage:true});}
  await page.getByRole('button',{name:'Network',exact:true}).click();
  assert.equal(await page.getByRole('button',{name:'Apply & Restart',exact:true}).isDisabled(),true);
  device.zigbee.progress=100;
  await page.getByRole('button',{name:'Firmware',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('[data-ui="radio-progress"]').value===100);
  assert.equal(await page.getByText(/CC2652 firmware transfer completed/).count(),0,'100% alone cannot confirm flashing');
  device.zigbee.busy=false;device.zigbee.paused=false;device.zigbee.result=3;
  await ready();await page.getByText('Zigbee operation failed. Open Diagnostics for details.',{exact:true}).waitFor();
  await page.getByRole('button',{name:'Flash Zigbee firmware',exact:true}).click();
  await page.getByRole('button',{name:'Flash Zigbee radio',exact:true}).click();
  await page.locator('[data-ui="radio-progress"]:not([hidden])').waitFor();
  await page.waitForFunction(()=>document.querySelector('[data-ui="radio-progress"]').value===55);
  // Native programming can finish before the shared script restores UART ownership.
  device.zigbee.result=2;device.zigbee.progress=100;
  await page.waitForFunction(()=>document.querySelector('[data-ui="radio-progress"]').value===100);
  assert.equal(await page.getByRole('button',{name:'Check for updates',exact:true}).isDisabled(),true,'Readback/restore is still busy');
  assert.equal(await page.getByText('Firmware flash complete.',{exact:true}).count(),0);
  device.zigbee.busy=false;device.zigbee.paused=false;
  await ready();await page.getByText(/CC2652 firmware transfer completed/).waitFor();
  await page.getByText('Firmware flash complete.',{exact:true}).waitFor();
  assert.equal(await page.locator('[data-ui="radio-progress"]').getAttribute('value'),'100');
  await page.locator('[data-ui="toast"]').waitFor({state:'hidden',timeout:8000});
  assert.equal(await page.locator('[data-ui="radio-progress"]').isVisible(),false);
  assert.equal(await page.getByText(/CC2652 firmware transfer completed/).isVisible(),true,'Keep the result in the firmware card after the banner clears');
  await page.getByRole('button',{name:'Zigbee',exact:true}).click();
  assert.equal(await page.getByText('Serial settings',{exact:true}).isVisible(),false);
  const beforeProbe=actions.length;
  await page.getByRole('button',{name:'Detect CC2652 variant',exact:true}).click();
  assert.equal(actions.length,beforeProbe);
  await page.getByRole('button',{name:'Detect variant',exact:true}).click();await ready();
  assert.deepEqual(actions.slice(beforeProbe).map(x=>x.body.action),['probe']);
  await page.getByRole('button',{name:'Zigbee firmware',exact:true}).click();
  await page.getByRole('button',{name:'Trigger Bootloader Manually',exact:true}).click();
  const beforeBootloader=actions.length;
  await page.getByRole('button',{name:'Cancel',exact:true}).click();assert.equal(actions.length,beforeBootloader);
  await page.getByRole('button',{name:'Trigger Bootloader Manually',exact:true}).click();
  await page.getByRole('button',{name:'Trigger bootloader',exact:true}).click();await ready();
  assert.equal(actions.at(-1).body.action,'bootloader');
  await page.getByRole('button',{name:'Zigbee',exact:true}).click();
  await page.getByRole('button',{name:'Reset Zigbee radio',exact:true}).click();
  await page.getByRole('button',{name:'Reset radio',exact:true}).click();await ready();
  assert.equal(actions.at(-1).body.action,'reset');
  await page.getByRole('button',{name:'Pause serial server',exact:true}).click();await ready();assert.equal(device.zigbee.paused,true);
  await page.getByRole('button',{name:'Resume serial server',exact:true}).click();await ready();assert.equal(device.zigbee.paused,false);
  if(process.env.TZB_SCREENSHOT_DIR)await page.screenshot({path:path.join(process.env.TZB_SCREENSHOT_DIR,'cc2652-radio.png'),fullPage:true});
  for(const width of [390,320]){
   await page.setViewportSize({width,height:900});
   for(const view of ['Overview','Zigbee','Network','Firmware','Diagnostics']){
    await page.getByRole('button',{name:view,exact:true}).click();
    assert.equal(await page.locator('#tzb-app').evaluate(el=>el.scrollWidth>el.clientWidth),false,view+' overflow at '+width);
   }
  }
  assert.deepEqual(errors,[]);
  console.log('CC2652 browser tests passed: P2 2021 identity, independent channels, failed-check guards, flash confirmation/progress/result, variant detection, bootloader/reset, device-managed flash pause/resume, hidden custom URL/baud settings, mobile layouts.');
 }finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
})().catch(error=>{console.error(error);process.exitCode=1;server.close();});
