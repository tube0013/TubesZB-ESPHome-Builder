const {componentsRoot}=require('./component_paths.cjs');
/* Exercises the real embedded page against a mock of the ESPHome HTTP endpoints. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const {chromium} = require('playwright');
const root = path.resolve(__dirname,'..');
const html = execFileSync(process.env.TZB_PYTHON_BIN || 'python3',[path.join(componentsRoot,'tubeszb_web/assets.py')],{cwd:root,encoding:'utf8'});
const baseDevice = () => ({nonce:'ui-token',name:'tubeszb-2026-zw',model:'TubesZB ZW2026',project_version:'2026.10.25.1',
  esphome_version:'2026.10.1',mac:'24:6F:28:A1:7C:50',ethernet_connected:true,api_connected:true,proxy_connected:true,
  baud_rate:115200,home_id:0x12345678,uptime:90061,free_heap:150000,update_state:0,latest_version:'',update_error:false,checking:false,action_pending:false});
const baseNetwork = () => ({mode:'static',testing:false,remaining:120,restarting:false,storage_ok:true,nonce:'network-token',
  ip:'192.168.1.50',subnet:'255.255.255.0',gateway:'192.168.1.1',dns1:'192.168.1.1',dns2:'0.0.0.0',
  return_ip:'192.168.1.50',hostname:'tubeszb-2026-zw.local',trial_pending:false,next_ip:''});
let device=baseDevice(),network=baseNetwork(),actions=[],streamCount=0,streams=new Set(),rejectAction=false,statusOffline=false,holdUpload=false,pendingUpload=null;
function sendLog(text) {
  const event=text.split('\n').map(line=>'data: '+line).join('\n');
  for(const response of streams)response.write('event: log\n'+event+'\n\n');
}
const server=http.createServer(async(req,res)=>{
  res.setHeader('Cache-Control','no-store');
  const url=new URL(req.url,'http://127.0.0.1');
  if(req.method==='GET'&&url.pathname==='/'){res.setHeader('Content-Type','text/html');res.end(html);return;}
  if(req.method==='GET'&&url.pathname==='/esphome'){res.setHeader('Content-Type','text/html');res.end('<h1>Original ESPHome interface</h1>');return;}
  if(req.method==='GET'&&url.pathname==='/events'){
    streamCount++;res.writeHead(200,{'Content-Type':'text/event-stream','Connection':'keep-alive'});
    res.write(': connected\n\n');streams.add(res);res.on('close',()=>streams.delete(res));return;
  }
  res.setHeader('Content-Type','application/json');
  if(req.method==='GET'&&url.pathname==='/tubeszb/status'){if(statusOffline){res.destroy();return;}res.end(JSON.stringify(device));return;}
  if(req.method==='GET'&&url.pathname==='/network/status'){res.end(JSON.stringify(network));return;}
  let body='';for await(const chunk of req)body+=chunk;
  actions.push({path:url.pathname,body,headers:req.headers});
  if(url.pathname==='/update'){res.setHeader('Content-Type','text/plain');if(holdUpload){pendingUpload=res;return;}res.end('Update Successful!');return;}
  assert.equal(req.headers[url.pathname.startsWith('/network/')?'x-tubeszb-network':'x-tubeszb-ui'],url.pathname.startsWith('/network/')?'network-token':'ui-token');
  if(rejectAction){res.writeHead(409);res.end('{"error":"Test action rejected"}');return;}
  if(url.pathname==='/network/confirm')network.testing=false;
  if(url.pathname==='/tubeszb/check-update'){device.update_state=1;device.latest_version=device.project_version;}
  res.end('{"ok":true}');
});

(async()=>{
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const base='http://127.0.0.1:'+server.address().port;
  const browser=await chromium.launch({headless:true,executablePath:process.env.TZB_CHROMIUM_PATH||undefined});
  try{
    const page=await browser.newPage({viewport:{width:1100,height:1000},acceptDownloads:true});
    const errors=[];page.on('pageerror',error=>errors.push(error.message));
    const load=async(view='overview')=>{
      await page.goto(base+'/?view='+view);
      await page.locator('[data-bind="name"]').first().filter({hasText:'tubeszb-2026-zw'}).waitFor();
      await page.waitForFunction(()=>document.querySelector('[data-bind="online"]').textContent==='Device online');
    };
    await load();
    assert.equal(streamCount,0,'The overview must not open a log stream');
    assert.equal(await page.locator('.tzb-brandmark svg').getAttribute('viewBox'),'43.4 0 17.1 20.6');
    assert.equal(await page.getByText('Z-Wave client connected',{exact:true}).first().isVisible(),true);
    if(process.env.TZB_SCREENSHOT_DIR){fs.mkdirSync(process.env.TZB_SCREENSHOT_DIR,{recursive:true});await page.screenshot({path:path.join(process.env.TZB_SCREENSHOT_DIR,'tubeszb-overview.png'),fullPage:true});await page.locator('.tzb-top').screenshot({path:path.join(process.env.TZB_SCREENSHOT_DIR,'tubeszb-header.png')});}
    await page.getByRole('button',{name:'Network',exact:true}).click();
    await page.getByLabel('IP address',{exact:true}).fill('999.1.2.3');
    await page.getByRole('button',{name:'Apply & Restart',exact:true}).click();
    assert.equal(actions.length,0);
    assert.match(await page.locator('[data-error="ip"]').textContent(),/four decimal/);
    await page.getByLabel('IP address',{exact:true}).fill('192.168.1.60');
    await page.getByRole('button',{name:'Apply & Restart',exact:true}).click();
    await page.locator('[data-ui="reconnect"]:not([hidden])').waitFor();
    assert.equal(actions.at(-1).path,'/network/apply');
    assert.match(await page.locator('[data-ui="reconnect-link"] a').getAttribute('href'),/192\.168\.1\.60/);
    network={...baseNetwork(),testing:true,ip:'192.168.1.60'};
    await load('network');
    await page.getByRole('button',{name:'Keep settings',exact:true}).click();
    await page.getByText('Network settings confirmed and saved.',{exact:true}).waitFor();
    assert.equal(actions.at(-1).path,'/network/confirm');
    await page.getByRole('button',{name:'Firmware',exact:true}).click();
    await page.getByRole('button',{name:'Check for updates',exact:true}).click();
    await page.getByText('You’re up to date.',{exact:true}).waitFor();
    assert.equal(actions.at(-1).path,'/tubeszb/check-update');

    // A pending update that never began must leave restart available immediately.
    device={...baseDevice(),action_pending:true,update_state:3,gateway_ota_state:0,restart_allowed:true};
    await load('overview');
    assert.equal(await page.getByRole('button',{name:'Restart device',exact:true}).isEnabled(),true);
    await page.getByRole('button',{name:'Restart device',exact:true}).click();
    await page.getByRole('dialog').waitFor();
    await page.getByRole('button',{name:'Cancel',exact:true}).click();
    device={...baseDevice(),gateway_ota_state:1,restart_allowed:false};await load('overview');
    assert.equal(await page.getByRole('button',{name:'Restart device',exact:true}).isDisabled(),true);
    device=baseDevice();await load('diagnostics');

    // Opening the shared dropdown captures once; section changes retain the stream and history.
    await page.getByRole('button',{name:'Diagnostics',exact:true}).click();
    assert.equal(streamCount,0);
    await page.locator('[data-ui="log-panel"] summary').click();
    assert.equal(await page.getByRole('button',{name:'Download logs',exact:true}).isDisabled(),true);
    await page.waitForFunction(()=>document.querySelector('[data-action="logs"]').textContent==='Stop capture');
    for(let i=0;i<30&&!streams.size;i++)await new Promise(resolve=>setTimeout(resolve,10));
    assert.equal(streams.size,1);
    sendLog('\x1b[32m[I][zwave_proxy] client subscribed\x1b[0m');
    sendLog('<img src=x onerror=alert(1)>');
    await page.getByRole('button',{name:'Download logs',exact:true}).waitFor();
    await page.waitForFunction(()=>!document.querySelector('[data-action="download-logs"]').disabled);
    assert.equal(await page.locator('[data-ui="logs"] img').count(),0,'Log messages must be rendered as text');
    const downloadPromise=page.waitForEvent('download');
    await page.getByRole('button',{name:'Download logs',exact:true}).click();
    const download=await downloadPromise;
    const downloaded=path.join(os.tmpdir(),'tzb-ui-captured-logs.txt');await download.saveAs(downloaded);
    const text=fs.readFileSync(downloaded,'utf8');
    assert.match(text,/tubeszb-2026-zw/);assert.match(text,/client subscribed/);assert.match(text,/Capture started:/);assert.equal(text.includes('\x1b'),false);
    // Bound both retained history and the visible DOM, while recording dropped lines.
    sendLog(Array.from({length:10050},(_,i)=>'diagnostic line '+i).join('\n'));
    await page.waitForFunction(()=>document.querySelector('[data-ui="log-summary"]').textContent.includes('older lines discarded'));
    assert.equal((await page.locator('[data-ui="logs"]').textContent()).split('\n').length,250);
    await page.getByRole('button',{name:'Overview',exact:true}).click();
    assert.equal(streams.size,1,'Overview hides the panel without stopping capture');
    sendLog('Captured while on Overview');
    await page.getByRole('button',{name:'Diagnostics',exact:true}).click();
    assert.equal(await page.getByRole('button',{name:'Download logs',exact:true}).isEnabled(),true);
    if(process.env.TZB_SCREENSHOT_DIR)await page.screenshot({path:path.join(process.env.TZB_SCREENSHOT_DIR,'tubeszb-logs.png'),fullPage:true});
    await page.getByRole('button',{name:'Clear',exact:true}).click();
    assert.equal(await page.getByRole('button',{name:'Download logs',exact:true}).isDisabled(),true);

    await page.getByRole('button',{name:'Overview',exact:true}).click();
    await page.getByRole('button',{name:'Restart device',exact:true}).click();
    await page.getByRole('dialog').waitFor();
    await page.getByRole('button',{name:'Cancel',exact:true}).click();
    await page.getByRole('button',{name:'Toggle light or dark appearance',exact:true}).click();
    if(process.env.TZB_SCREENSHOT_DIR)await page.screenshot({path:path.join(process.env.TZB_SCREENSHOT_DIR,'tubeszb-dark.png'),fullPage:true});
    await page.getByRole('button',{name:'Toggle light or dark appearance',exact:true}).click();
    await page.setViewportSize({width:390,height:900});
    for(const view of ['Overview','Network','Firmware','Diagnostics']){
      await page.getByRole('button',{name:view,exact:true}).click();
      assert.equal(await page.locator('#tzb-app').evaluate(element=>element.scrollWidth>element.clientWidth),false,view+' overflows');
    }
    // Legacy UI remains reachable on the same server.
    await page.setViewportSize({width:1100,height:1000});
    await page.getByRole('button',{name:'ESPHome interface',exact:true}).click();
    await page.getByRole('heading',{name:'Original ESPHome interface'}).waitFor();

    device={...baseDevice(),update_state:2,latest_version:'2026.10.26.0'};network=baseNetwork();
    await load('firmware');
    const beforeInstall=actions.length;
    await page.getByRole('button',{name:'Install update',exact:true}).click();
    assert.equal(actions.length,beforeInstall,'Installing requires the review dialog action');
    await page.getByRole('button',{name:'Install firmware',exact:true}).click();
    await page.getByText('Firmware installation requested. Do not disconnect power.',{exact:true}).waitFor();
    assert.equal(actions.at(-1).path,'/tubeszb/install-update');
    // Native OTA percentages remain available while the gateway main loop flashes.
    device={...baseDevice(),update_state:3,gateway_ota_state:1,gateway_ota_progress:-1};await load('firmware');
    assert.equal(await page.getByRole('progressbar',{name:'Gateway firmware installation progress',exact:true}).getAttribute('value'),null);
    device.gateway_ota_progress=37.8;
    await page.waitForFunction(()=>document.querySelector('[data-ui="gateway-progress"]').value===37);
    assert.equal(await page.getByRole('button',{name:'Choose file',exact:true}).isDisabled(),true);
    assert.equal(await page.getByRole('button',{name:'Check for updates',exact:true}).isDisabled(),true);
    statusOffline=true;
    await page.getByText(/Connection interrupted during firmware installation/).waitFor();
    assert.equal(await page.locator('[data-ui="gateway-progress"]').getAttribute('value'),'37','A disconnect must not invent completion');
    statusOffline=false;device.gateway_ota_progress=84;
    await page.waitForFunction(()=>document.querySelector('[data-ui="gateway-progress"]').value===84);
    device.gateway_ota_state=2;device.gateway_ota_progress=100;
    await page.getByText('Firmware verified. The gateway is restarting.',{exact:true}).waitFor();
    assert.equal(await page.locator('[data-ui="gateway-progress"]').getAttribute('value'),'100');
    await page.locator('[data-ui="reconnect"]:not([hidden])').waitFor();
    device={...baseDevice(),gateway_ota_state:3,gateway_ota_progress:42,gateway_ota_error:128};await load('firmware');
    await page.getByText(/Firmware installation failed or was aborted \(OTA error 128\)/).waitFor();
    assert.equal(await page.locator('[data-ui="gateway-progress"]').getAttribute('value'),'42');
    assert.equal(await page.getByRole('button',{name:'Check for updates',exact:true}).isEnabled(),true);
    // An observed reboot clears pending controls without claiming the image was installed.
    device={...baseDevice(),update_state:3,gateway_ota_state:1,gateway_ota_progress:90};await load('firmware');
    await page.waitForFunction(()=>document.querySelector('[data-ui="gateway-progress"]').value===90);
    device={...baseDevice(),nonce:'new-boot-token'};
    await page.getByText('Gateway restarted. Reported firmware: 2026.10.25.1.',{exact:true}).waitFor();
    assert.equal(await page.getByRole('button',{name:'Choose file',exact:true}).isEnabled(),true);
    device=baseDevice();rejectAction=true;await load('firmware');
    await page.getByRole('button',{name:'Check for updates',exact:true}).click();
    await page.getByText('Test action rejected',{exact:true}).waitFor();
    rejectAction=false;

    device={...baseDevice(),proxy_connected:false,api_connected:false};network=baseNetwork();
    await load();
    assert.equal(await page.getByText('Waiting for a Z-Wave client',{exact:true}).first().isVisible(),true);
    network={...baseNetwork(),testing:true};await load('firmware');
    assert.equal(await page.getByRole('button',{name:'Check for updates',exact:true}).isDisabled(),true);
    network=baseNetwork();await load('firmware');
    await page.locator('#tzb-firmware-file').setInputFiles({name:'firmware.factory.bin',mimeType:'application/octet-stream',buffer:Buffer.from([0xe9,0,0])});
    await page.getByRole('button',{name:'Review installation',exact:true}).click();
    await page.getByText(/Factory images must be flashed over USB/).waitFor();
    assert.equal(actions.some(action=>action.path==='/update'),false);
    // Gateway uploads release the log socket but retain its browser history.
    await page.locator('[data-ui="log-panel"] summary').click();
    for(let i=0;i<100&&!streams.size;i++)await new Promise(resolve=>setTimeout(resolve,10));
    assert.equal(streams.size,1);sendLog('retained before gateway upload');
    await page.waitForFunction(()=>document.querySelector('[data-ui="logs"]').textContent.includes('retained before gateway upload'));
    holdUpload=true;
    await page.locator('#tzb-firmware-file').setInputFiles({name:'firmware.ota.bin',mimeType:'application/octet-stream',buffer:Buffer.from([0xe9,0,0])});
    await page.getByRole('button',{name:'Review installation',exact:true}).click();
    await page.getByRole('button',{name:'Upload firmware',exact:true}).click();
    await page.getByText('Upload sent. Waiting for device verification — keep power connected.',{exact:true}).waitFor();
    assert.equal(await page.locator('[data-ui="upload-progress"]').getAttribute('value'),'100');
    assert.equal(await page.getByRole('button',{name:'Choose file',exact:true}).isDisabled(),true);
    assert.equal(await page.locator('[data-ui="reconnect"]').isVisible(),false,'100% uploaded does not confirm verification');
    for(let i=0;i<100&&streams.size;i++)await new Promise(resolve=>setTimeout(resolve,10));
    assert.equal(streams.size,0,'Gateway upload must release the log connection');
    assert.equal(await page.getByRole('button',{name:'Start capture',exact:true}).isDisabled(),true);
    assert.match(await page.locator('[data-ui="logs"]').textContent(),/retained before gateway upload/);
    assert.match(await page.locator('[data-ui="log-notice"]').textContent(),/stopped for gateway firmware upload/);
    assert.ok(pendingUpload);pendingUpload.end('Update Failed');pendingUpload=null;
    await page.getByText('Upload was not confirmed: Update Failed Check whether the device restarted before trying again.',{exact:true}).first().waitFor();
    assert.equal(await page.locator('[data-ui="reconnect"]').isVisible(),false);
    assert.equal(await page.getByRole('button',{name:'Choose file',exact:true}).isEnabled(),true);
    await page.getByRole('button',{name:'Review installation',exact:true}).click();
    await page.getByRole('button',{name:'Upload firmware',exact:true}).click();
    await page.getByText('Upload sent. Waiting for device verification — keep power connected.',{exact:true}).waitFor();
    assert.ok(pendingUpload);pendingUpload.end('Update Successful!');pendingUpload=null;
    await page.getByText('Firmware uploaded successfully. The gateway is restarting.',{exact:true}).waitFor();
    assert.equal(actions.at(-1).path,'/update');
    assert.deepEqual(errors,[]);
    console.log('Live-page browser tests passed: logo, network API, updates, buffered log export, SSE cleanup, safe text rendering, native OTA progress/failure/reboot, upload progress and verification failure/retry, legacy UI, theme, mobile layout.');
  }finally{
    await browser.close();for(const response of streams)response.end();await new Promise(resolve=>server.close(resolve));
  }
})().catch(error=>{console.error(error);process.exitCode=1;server.close();});
