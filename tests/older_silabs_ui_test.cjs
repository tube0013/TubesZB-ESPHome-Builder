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
let device={};let actions=[];let connections=0;const streams=new Set();
const network={nonce:'network-token',mode:'dhcp',ip:'192.168.1.55',gateway:'192.168.1.1',subnet:'255.255.255.0',
 dns1:'192.168.1.1',dns2:'0.0.0.0',hostname:'tubeszb.local',storage_ok:true,testing:false,restarting:false};
const server=http.createServer((req,res)=>{
 const url=new URL(req.url,'http://127.0.0.1');res.setHeader('Cache-Control','no-store');
 if(url.pathname==='/events'){
  connections++;res.writeHead(200,{'Content-Type':'text/event-stream','Connection':'keep-alive'});
  streams.add(res);res.on('close',()=>streams.delete(res));res.write(': connected\n\n');return;
 }
 if(url.pathname==='/tubeszb/zigbee-action'){
  let body='';req.on('data',chunk=>body+=chunk);req.on('end',()=>{
   const args=Object.fromEntries(new URLSearchParams(body));actions.push(args);device.zigbee.result=2;device.zigbee.operation='settings';
   assert.equal(req.headers['x-tubeszb-ui'],'log-token');
   if(args.action==='url'){device.zigbee.custom_url=args.value;device.zigbee.checked=false;}
   if(args.action==='pause')device.zigbee.paused=true;
   if(args.action==='resume')device.zigbee.paused=false;
   if(args.action==='baud'){device.zigbee.baud=args.value;device.baud_rate=Number(args.value);}
   if(args.action==='gateway-channel'){device.zigbee.gateway_beta=args.value==='beta';device.update_verified=false;}
   if(args.action==='zigbee-channel'){device.zigbee.zigbee_beta=args.value==='beta';device.zigbee.checked=false;}
   if(args.action==='check')device.zigbee.checked=true;
   if(args.action==='flash'){device.zigbee.operation='flash';device.zigbee.result=1;device.zigbee.busy=true;device.zigbee.progress=25;}
   if(args.action==='bootloader'){device.zigbee.operation='bootloader';device.zigbee.result=2;device.zigbee.busy=false;}
   res.setHeader('Content-Type','application/json');res.end('{"ok":true}');
  });return;
 }
 if(url.pathname==='/network/internet'){
  let body='';req.on('data',chunk=>body+=chunk);req.on('end',()=>{
   network.use_without_internet=new URLSearchParams(body).get('use_without_internet')==='true';
   res.setHeader('Content-Type','application/json');res.end('{"ok":true}');
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
  const profiles=[
   ['tubeszb-efr32-mgm210-poe-2022','TubesZB MGM210 PoE 2022','MGM210',true],
   ['tubeszb-efr32-mgm210-poe-2023','TubesZB MGM210 PoE 2023','MGM210',true],
   ['tubeszb-efr32-mgm12-pro1','TubesZB MGM12 Pro1','MGM12',true],
   ['tubeszb-efr32-mgm210-series-2','TubesZB MGM210 Series 2','MGM210',true]
  ];
  for(const [name,model,radio,selectable] of profiles){
   const type='efr32';
   device={nonce:'log-token',name:'tubeszb-'+type,model:'TubesZB '+type,radio_type:type,project_version:'2026.10.25.1',esphome_version:'2026.10.1',
    mac:'24:6F:28:A1:7C:55',board:'esp-wrover-kit',ethernet_connected:true,api_connected:true,proxy_connected:true,baud_rate:115200,
    home_id:0,uptime:1000,free_heap:140000,update_state:1,latest_version:'2026.10.25.1',update_error:false,checking:false,action_pending:false,
    zigbee:{busy:false,connected:true,paused:false,firmware:'20250321',latest:'20250321',board_name:type==='cc2652'?'CC2652P2':'BM24',
     manufacturer:'TubesZB',ieee:'00124b0000000001',info_status:'Ready',custom_url:'',baud:'115200',baud_options:['115200'],
     gateway_beta:false,zigbee_beta:false,checked:false,operation:'',result:0,progress:-1,port:6638}};


   actions=[];network.use_without_internet=false;device.name=name;device.model=model;device.dual_radio=false;
   device.zigbee.board_name=radio;device.zigbee.manufacturer='Silicon Labs';device.zigbee.firmware='7.4.3';device.zigbee.ieee='00124b0000000001';device.zigbee.supports_zigbee_beta=false;
   device.zigbee.info_status='Probe complete';device.zigbee.baud='115200';device.zigbee.baud_options=selectable?['115200','230400','460800','921600']:[];
   const page=await browser.newPage({viewport:{width:1100,height:1000}});const errors=[];page.on('pageerror',e=>errors.push(e.message));
   await page.goto('http://127.0.0.1:'+server.address().port+'/?view=overview');await page.getByText('Device online',{exact:true}).waitFor();
   assert.equal(await page.getByRole('heading',{name:'Zigbee gateway',exact:true}).isVisible(),true);
   assert.equal(await page.locator('[data-view=overview]').getByText(model,{exact:true}).isVisible(),true);
   assert.equal(await page.getByRole('button',{name:'Z-Wave',exact:true}).isVisible(),false);
   assert.equal(await page.locator('[data-ui="log-panel"]').isVisible(),false);
   await page.getByRole('button',{name:'Zigbee',exact:true}).click();
   assert.equal(await page.locator('[data-view=zigbee] [data-bind="radio-board"]').textContent(),radio);
   assert.equal(await page.getByRole('button',{name:'Probe radio info',exact:true}).isVisible(),true);
   const before=actions.length;await page.getByRole('button',{name:'Reset Zigbee radio',exact:true}).click();assert.equal(actions.length,before);
   await page.getByRole('button',{name:'Reset radio',exact:true}).click();await page.waitForFunction(()=>!document.querySelector('[data-action="radio-reset"]').disabled);
   assert.equal(actions.at(-1).action,'reset');
   await page.getByRole('button',{name:'Pause serial server',exact:true}).click();await page.waitForFunction(()=>!document.querySelector('[data-action="serial-resume"]').disabled);
   assert.equal(device.zigbee.paused,true);await page.getByRole('button',{name:'Resume serial server',exact:true}).click();await page.waitForFunction(()=>!document.querySelector('[data-action="serial-pause"]').disabled);
   if(selectable){
    await page.getByText('Serial settings',{exact:true}).click();await page.locator('#tzb-radio-baud').selectOption('460800');
    await page.getByRole('button',{name:'Apply baud rate',exact:true}).click();await page.waitForFunction(()=>!document.querySelector('[data-action="radio-baud"]').disabled);
    assert.equal(actions.at(-1).value,'460800');assert.equal(await page.locator('[data-view=zigbee] [data-bind="baud"]').textContent(),'460800');
   }else{
    assert.equal(await page.getByText('Serial settings',{exact:true}).isVisible(),false);
    assert.equal(await page.locator('[data-action="radio-baud"]').isDisabled(),true);
    assert.equal(await page.locator('[data-view=zigbee] [data-bind="baud"]').textContent(),'115200');
   }
   await page.locator('[data-ui="log-panel"] summary').click();await until(()=>streams.size===1);const count=connections;
   emit('older profile '+name);await page.waitForFunction(name=>document.querySelector('[data-ui="logs"]').textContent.includes(name),name);
   await page.getByRole('button',{name:'Firmware',exact:true}).click();
   assert.equal(await page.getByRole('button',{name:'Check for updates',exact:true}).isEnabled(),true);
   assert.equal(await page.getByRole('button',{name:'Choose file',exact:true}).isEnabled(),true);
   assert.equal(await page.locator('[data-ui="zigbee-firmware"]').isVisible(),true);
   assert.equal(await page.getByLabel('Use beta Zigbee firmware',{exact:true}).isVisible(),false,'No beta channel exists for these legacy radio images');
   assert.equal(await page.getByLabel('Use beta gateway firmware',{exact:true}).isVisible(),true);
   assert.equal(await page.getByRole('button',{name:'Flash Zigbee firmware',exact:true}).isDisabled(),true);
   await page.getByRole('button',{name:'Check Zigbee firmware',exact:true}).click();
   await page.waitForFunction(()=>!document.querySelector('[data-action="radio-flash"]').disabled);
   const beforeFlash=actions.length;
   await page.getByRole('button',{name:'Flash Zigbee firmware',exact:true}).click();
   assert.equal(actions.length,beforeFlash);
   await page.getByRole('button',{name:'Flash Zigbee radio',exact:true}).click();
   await page.waitForFunction(()=>document.querySelector('[data-ui="radio-progress"]').value===25);
   assert.equal(await page.locator('[data-ui="radio-progress"]').evaluate(el=>Boolean(el.closest('[data-ui="toast"]'))),true);
   device.zigbee.progress=100;device.zigbee.busy=false;device.zigbee.result=2;
   await page.getByText('Firmware flash complete.',{exact:true}).waitFor();
   await page.waitForFunction(()=>document.querySelector('[data-ui="toast"]').hidden,{},{timeout:12000});
   await page.getByText('Custom radio firmware source',{exact:true}).click();
   await page.getByLabel('GBL firmware URL',{exact:true}).fill('https://example.invalid/radio.gbl');
   await page.getByRole('button',{name:'Save custom URL',exact:true}).click();
   await page.waitForFunction(()=>!document.querySelector('[data-action="radio-url-clear"]').disabled);
   assert.equal(device.zigbee.custom_url,'https://example.invalid/radio.gbl');
   assert.equal(await page.getByRole('button',{name:'Check Zigbee firmware',exact:true}).isDisabled(),true);
   await page.getByRole('button',{name:'Use selected channel',exact:true}).click();
   await page.waitForFunction(()=>!document.querySelector('[data-action="radio-check"]').disabled);
   assert.equal(await page.getByRole('button',{name:'Trigger Bootloader Manually',exact:true}).isVisible(),true);
   await page.getByRole('button',{name:'Network',exact:true}).click();await page.getByRole('switch',{name:'Use without internet',exact:true}).click();
   await page.getByRole('button',{name:'Use without internet',exact:true}).click();await page.getByText('Use without internet enabled and saved.',{exact:true}).waitFor();
   await page.getByRole('button',{name:'Firmware',exact:true}).click();assert.equal(await page.getByRole('button',{name:'Check for updates',exact:true}).isDisabled(),true);
   assert.equal(await page.locator('[data-ui="offline-reminder"]').isVisible(),true);assert.equal(await page.getByRole('button',{name:'Choose file',exact:true}).isEnabled(),true);
   await page.getByRole('button',{name:'Diagnostics',exact:true}).click();assert.equal(connections,count);assert.match(await page.locator('[data-ui="logs"]').textContent(),new RegExp(name));
   for(const width of [390,320]){
    await page.setViewportSize({width,height:900});
    for(const view of ['Overview','Zigbee','Network','Firmware','Diagnostics']){await page.getByRole('button',{name:view,exact:true}).click();assert.equal(await page.locator('#tzb-app').evaluate(el=>el.scrollWidth>el.clientWidth),false,name+' '+view+' at '+width);}
   }
   if(process.env.TZB_SCREENSHOT_DIR){
    fs.mkdirSync(process.env.TZB_SCREENSHOT_DIR,{recursive:true});await page.emulateMedia({colorScheme:'dark'});await page.setViewportSize({width:1100,height:1000});
    await page.getByRole('button',{name:'Zigbee',exact:true}).click();await page.screenshot({path:path.join(process.env.TZB_SCREENSHOT_DIR,name+'-zigbee.png'),fullPage:true});
   }
   await page.close();await until(()=>streams.size===0);assert.deepEqual(errors,[]);
  }
  console.log('Older Silicon Labs UI checks passed for MGM210 PoE 2022/2023, MGM12 Pro1 and MGM210 Series 2: identity/probe controls, reset confirmation, saved baud, stable-only radio flashing/progress/completion, direct GBL URLs, bootloader controls, local OTA, offline mode, retained logs and mobile layouts.');
 }finally{await browser.close();for(const stream of streams)stream.end();await new Promise(r=>server.close(r));}
})().catch(error=>{console.error(error);process.exitCode=1;for(const stream of streams)stream.end();server.close();});
