const {componentsRoot}=require('./component_paths.cjs');
const assert=require('node:assert/strict');
const http=require('node:http');
const fs=require('node:fs');
const path=require('node:path');
const {execFileSync}=require('node:child_process');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'..');
const html=execFileSync(process.env.TZB_PYTHON_BIN||'python3',[path.join(componentsRoot,'tubeszb_web/assets.py')],{cwd:root,encoding:'utf8'});
let device={nonce:'ui-token',name:'tubeszb-mgm24-bp',model:'TubesZB MGM24 BP',board:'esp-wrover-kit',radio_type:'efr32',
 project_version:'2026.10.25.1',esphome_version:'2026.10.1',mac:'24:6F:28:A1:7C:51',ethernet_connected:true,api_connected:true,
 proxy_connected:true,baud_rate:115200,home_id:0,uptime:5000,free_heap:140000,update_state:2,latest_version:'old-source',
 update_error:false,checking:false,update_verified:false,action_pending:false,
 zigbee:{busy:false,connected:true,paused:false,firmware:'8.2.2.0',latest:'8.2.2.0',board_name:'TubesZB MGM24',manufacturer:'TubesZB',
 ieee:'00124b0000000001',info_status:'Probe complete',custom_url:'',baud:'115200',baud_options:['115200','230400','460800','921600'],
 gateway_beta:false,zigbee_beta:false,checked:false,operation:'',result:0,progress:-1,port:6638}};
const network={mode:'static',testing:false,remaining:120,restarting:false,storage_ok:true,nonce:'network-token',ip:'192.168.1.51',
 subnet:'255.255.255.0',gateway:'192.168.1.1',dns1:'192.168.1.1',dns2:'0.0.0.0',return_ip:'192.168.1.51',hostname:'tubeszb-mgm24-bp.local'};
const actions=[];
const server=http.createServer(async(req,res)=>{
 const url=new URL(req.url,'http://127.0.0.1');res.setHeader('Cache-Control','no-store');
 if(req.method==='GET'&&url.pathname==='/'){res.setHeader('Content-Type','text/html');res.end(html);return;}
 res.setHeader('Content-Type','application/json');
 if(req.method==='GET'&&url.pathname==='/tubeszb/status'){res.end(JSON.stringify(device));return;}
 if(req.method==='GET'&&url.pathname==='/network/status'){res.end(JSON.stringify(network));return;}
 let raw='';for await(const chunk of req)raw+=chunk;
 const body=Object.fromEntries(new URLSearchParams(raw));actions.push({path:url.pathname,body});
 assert.equal(req.headers['x-tubeszb-ui'],'ui-token');
 if(url.pathname==='/tubeszb/check-update'){device.update_verified=true;device.update_state=1;device.latest_version=device.project_version;}
 if(url.pathname==='/tubeszb/zigbee-action'){
  const radio=device.zigbee;
  radio.result=2;radio.operation='settings';
  switch(body.action){
   case 'gateway-channel':radio.gateway_beta=body.value==='beta';device.update_verified=false;device.update_state=2;break;
   case 'zigbee-channel':radio.zigbee_beta=body.value==='beta';radio.checked=false;break;
   case 'check':radio.checked=true;radio.operation='check';radio.latest=radio.zigbee_beta?'8.3.0.0-beta':'8.2.2.0';break;
   case 'flash':radio.operation='flash';radio.result=1;radio.busy=true;radio.progress=30;break;
   case 'probe':radio.info_status='Probe complete';break;
   case 'bootloader':radio.operation='bootloader';radio.result=1;radio.busy=true;break;
   case 'reset':radio.info_status='Radio reset';break;
   case 'pause':radio.paused=true;break;
   case 'resume':radio.paused=false;break;
   case 'baud':radio.baud=body.value;device.baud_rate=Number(body.value);break;
   case 'url':radio.custom_url=body.value;radio.checked=false;break;
  }
 }
 res.end('{"ok":true}');
});
(async()=>{
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 const browser=await chromium.launch({headless:true,executablePath:process.env.TZB_CHROMIUM_PATH||undefined});
 try{
  const page=await browser.newPage({viewport:{width:1100,height:1150}});const errors=[];
  page.on('pageerror',error=>errors.push(error.message));
  await page.goto('http://127.0.0.1:'+server.address().port+'/?view=overview');
  await page.getByText('Zigbee client connected',{exact:true}).waitFor();
  assert.equal(await page.getByRole('heading',{name:'Zigbee gateway',exact:true}).isVisible(),true);
  assert.equal(await page.getByText('esp-wrover-kit',{exact:true}).isVisible(),true);
  assert.equal(await page.getByText('tubeszb-mgm24-bp',{exact:true}).first().isVisible(),true);
  if(process.env.TZB_SCREENSHOT_DIR){fs.mkdirSync(process.env.TZB_SCREENSHOT_DIR,{recursive:true});await page.screenshot({path:path.join(process.env.TZB_SCREENSHOT_DIR,'mgm24-overview.png'),fullPage:true});}
  const ready=()=>page.waitForFunction(()=>!document.querySelector('[data-action="check-update"]').disabled);
  await page.getByRole('button',{name:'Firmware',exact:true}).click();
  assert.deepEqual(await page.locator('[data-view=firmware] h2:visible').allTextContents(),['TubesZB firmware','Install a firmware file','Zigbee radio firmware']);
  assert.equal(await page.getByRole('button',{name:'Install update',exact:true}).isVisible(),false,'An old-source ESP release cannot be installed');
  assert.equal(await page.getByRole('button',{name:'Flash Zigbee firmware',exact:true}).isDisabled(),true);
  await page.getByLabel('Use beta gateway firmware',{exact:true}).check();await ready();
  assert.equal(actions.at(-1).body.action,'gateway-channel');assert.equal(actions.at(-1).body.value,'beta');
  await page.getByText('Gateway firmware channel set to beta. Check for updates before installing.',{exact:true}).waitFor();
  await page.getByLabel('Use beta gateway firmware',{exact:true}).uncheck();await ready();
  assert.equal(device.zigbee.gateway_beta,false);
  await page.getByText('Gateway firmware channel set to stable. Check for updates before installing.',{exact:true}).waitFor();
  await page.getByLabel('Use beta gateway firmware',{exact:true}).check();await ready();
  assert.equal(await page.getByRole('button',{name:'Install update',exact:true}).isVisible(),false);
  await page.getByRole('button',{name:'Check for updates',exact:true}).click();
  await page.getByText('You’re up to date.',{exact:true}).waitFor();
  assert.equal(actions.at(-1).path,'/tubeszb/check-update');
  assert.equal(device.zigbee.zigbee_beta,false,'Gateway beta must not change the radio channel');
  await page.getByLabel('Use beta Zigbee firmware',{exact:true}).check();await ready();
  assert.equal(actions.at(-1).body.action,'zigbee-channel');assert.equal(device.zigbee.gateway_beta,true);
  assert.equal(await page.getByText('Not checked for this source',{exact:true}).isVisible(),true);
  await page.getByRole('button',{name:'Check Zigbee firmware',exact:true}).click();await ready();
  await page.getByText('8.3.0.0-beta',{exact:true}).waitFor();
  assert.equal(actions.at(-1).body.action,'check');
  const beforeFlash=actions.length;
  await page.getByRole('button',{name:'Flash Zigbee firmware',exact:true}).click();
  assert.equal(actions.length,beforeFlash,'Radio flashing requires explicit confirmation');
  await page.getByRole('button',{name:'Flash Zigbee radio',exact:true}).click();
  await page.locator('[data-ui="radio-progress"]:not([hidden])').waitFor();
  await page.waitForFunction(()=>document.querySelector('[data-ui="radio-progress"]').value===30);
  assert.equal(await page.locator('[data-ui="radio-progress"]').getAttribute('value'),'30');
  assert.equal(await page.locator('[data-ui="radio-progress"]').evaluate(el=>Boolean(el.closest('[data-ui="toast"]'))),true,'Radio progress belongs in the top banner');
  assert.equal(await page.getByLabel('Use beta gateway firmware',{exact:true}).isDisabled(),true);
  await page.getByRole('button',{name:'Network',exact:true}).click();
  assert.equal(await page.getByRole('button',{name:'Apply & Restart',exact:true}).isDisabled(),true);
  device.zigbee.busy=false;device.zigbee.result=2;device.zigbee.progress=100;
  await page.getByRole('button',{name:'Firmware',exact:true}).click();await ready();
  await page.getByText(/Radio firmware transfer completed/).waitFor();
  await page.getByText('Firmware flash complete.',{exact:true}).waitFor();
  assert.equal(await page.locator('[data-ui="radio-progress"]').getAttribute('value'),'100');
  await page.locator('[data-ui="toast"]').waitFor({state:'hidden',timeout:8000});
  assert.equal(await page.locator('[data-ui="radio-progress"]').isVisible(),false);
  assert.equal(await page.getByText(/Radio firmware transfer completed/).isVisible(),true,'Keep the result in the firmware card after the banner clears');
  if(process.env.TZB_SCREENSHOT_DIR)await page.screenshot({path:path.join(process.env.TZB_SCREENSHOT_DIR,'mgm24-firmware.png'),fullPage:true});
  await page.getByText('Custom radio firmware source',{exact:true}).click();
  for(const url of ['javascript:alert(1)','https://example.invalid/manifest.json','https://example.invalid/radio.bin','https://example.invalid/radio.hex','https://example.invalid/download?file=radio.gbl']){
   await page.getByLabel('GBL firmware URL',{exact:true}).fill(url);
   const beforeURL=actions.length;
   await page.getByRole('button',{name:'Save custom URL',exact:true}).click();
   assert.equal(actions.length,beforeURL,'Reject non-GBL custom URL: '+url);
   await page.getByText('Enter a direct HTTP(S) .gbl file URL, or leave it empty.',{exact:true}).waitFor();
  }
  await page.getByLabel('GBL firmware URL',{exact:true}).fill('https://example.invalid/radio.GBL?download=1#firmware');
  await page.getByRole('button',{name:'Save custom URL',exact:true}).click();await ready();
  assert.equal(device.zigbee.custom_url,'https://example.invalid/radio.GBL?download=1#firmware');
  assert.equal(await page.getByRole('button',{name:'Flash Zigbee firmware',exact:true}).isEnabled(),true);
  assert.equal(await page.getByRole('button',{name:'Check Zigbee firmware',exact:true}).isDisabled(),true);
  await page.getByText('Custom .gbl image selected. Review and flash it directly.',{exact:true}).waitFor();
  await page.getByLabel('GBL firmware URL',{exact:true}).fill('https://example.invalid/radio.gbl');
  await page.getByRole('button',{name:'Save custom URL',exact:true}).click();await ready();
  assert.equal(actions.at(-1).body.action,'url');
  assert.equal(device.zigbee.zigbee_beta,true);
  assert.equal(await page.getByRole('button',{name:'Flash Zigbee firmware',exact:true}).isEnabled(),true);
  await page.getByLabel('Use beta Zigbee firmware',{exact:true}).uncheck();await ready();
  assert.equal(device.zigbee.custom_url,'https://example.invalid/radio.gbl','Changing channel must preserve a custom source override');
  await page.getByRole('button',{name:'Use selected channel',exact:true}).click();await ready();
  assert.equal(device.zigbee.custom_url,'');assert.equal(device.zigbee.zigbee_beta,false);
  const beforeBootloader=actions.length;
  await page.getByRole('button',{name:'Trigger Bootloader Manually',exact:true}).click();
  await page.getByRole('button',{name:'Cancel',exact:true}).click();
  assert.equal(actions.length,beforeBootloader,'Opening/cancelling must not toggle hardware');
  await page.getByRole('button',{name:'Trigger Bootloader Manually',exact:true}).click();
  await page.getByRole('button',{name:'Trigger bootloader',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('[data-bind="radio-busy-label"]').textContent==='Busy');
  assert.equal(actions.at(-1).body.action,'bootloader');
  assert.equal(await page.getByLabel('Use beta Zigbee firmware',{exact:true}).isDisabled(),true);
  await page.getByRole('button',{name:'Network',exact:true}).click();
  assert.equal(await page.getByRole('button',{name:'Apply & Restart',exact:true}).isDisabled(),true);
  device.zigbee.busy=false;device.zigbee.result=2;
  await page.getByRole('button',{name:'Firmware',exact:true}).click();await ready();
  await page.getByText('Manual bootloader ready. Use an external flasher, or reset the radio to resume normal operation.',{exact:true}).waitFor();
  // The same EFR32 controls apply to the BM24 identity reported by the radio.
  device.zigbee.board_name='TubesZB BM24';
  await page.getByRole('button',{name:'Zigbee',exact:true}).click();
  await page.getByText('TubesZB BM24',{exact:true}).first().waitFor();
  await page.getByRole('button',{name:'Zigbee firmware',exact:true}).click();
  assert.equal(await page.getByRole('button',{name:'Trigger Bootloader Manually',exact:true}).isEnabled(),true);
  await page.getByRole('button',{name:'Zigbee',exact:true}).click();
  await page.getByRole('button',{name:'Reset Zigbee radio',exact:true}).click();
  await page.getByRole('button',{name:'Reset radio',exact:true}).click();await ready();
  assert.equal(actions.at(-1).body.action,'reset');
  await page.getByRole('button',{name:'Firmware',exact:true}).click();

  assert.equal(await page.getByRole('button',{name:'Flash Zigbee firmware',exact:true}).isDisabled(),true);
  await page.getByRole('button',{name:'Zigbee',exact:true}).click();
  await page.getByRole('button',{name:'Probe radio info',exact:true}).click();
  await page.getByRole('button',{name:'Probe radio',exact:true}).click();await ready();
  assert.equal(actions.at(-1).body.action,'probe');
  await page.getByRole('button',{name:'Pause serial server',exact:true}).click();await ready();
  assert.equal(device.zigbee.paused,true);
  await page.getByRole('button',{name:'Resume serial server',exact:true}).click();await ready();
  assert.equal(device.zigbee.paused,false);
  await page.getByText('Serial settings',{exact:true}).click();
  await page.getByLabel('Baud rate',{exact:true}).selectOption('460800');
  // An unchanged native baud must not wipe an unsaved user selection on polls.
  await page.waitForTimeout(1800);
  assert.equal(await page.getByLabel('Baud rate',{exact:true}).inputValue(),'460800');
  await page.getByRole('button',{name:'Apply baud rate',exact:true}).click();await ready();
  assert.equal(actions.at(-1).body.action,'baud');assert.equal(device.baud_rate,460800);
  // A manifest-driven change after flashing must refresh the visible selection.
  device.zigbee.baud='115200';device.baud_rate=115200;
  await page.waitForFunction(()=>document.querySelector('#tzb-radio-baud').value==='115200');
  assert.equal(await page.getByLabel('Baud rate',{exact:true}).inputValue(),'115200');
  if(process.env.TZB_SCREENSHOT_DIR)await page.screenshot({path:path.join(process.env.TZB_SCREENSHOT_DIR,'mgm24-radio.png'),fullPage:true});
  for(const width of [390,320]){
   await page.setViewportSize({width,height:900});
   for(const view of ['Overview','Zigbee','Network','Firmware','Diagnostics']){
    await page.getByRole('button',{name:view,exact:true}).click();
    assert.equal(await page.locator('#tzb-app').evaluate(el=>el.scrollWidth>el.clientWidth),false,view+' overflow at '+width);
   }
  }
  assert.equal(await page.getByText('The original ESPHome interface is also available from the navigation menu.',{exact:true}).isVisible(),true);
  assert.equal(await page.getByText('The device could not complete the request.',{exact:true}).count(),0);
  assert.deepEqual(errors,[]);
  console.log('MGM24 browser tests passed: BP identity, independent beta channels, stale-source guards, Zigbee flashing/progress, probe, serial controls, baud, custom URLs, and mobile layouts.');
 }finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
})().catch(error=>{console.error(error);process.exitCode=1;server.close();});
