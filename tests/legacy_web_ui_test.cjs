const assert=require('node:assert/strict');
const fs=require('node:fs');
const http=require('node:http');
const path=require('node:path');
const {chromium}=require('playwright');
const bundle=fs.readFileSync(path.join(__dirname,'../www3TZB.js'),'utf8');
const requests=[],actions=[],streams=new Set();
const entities=[
 {id:'text_sensor/ESP IP Address',name:'ESP IP Address',state:'192.168.1.51',value:'192.168.1.51',sorting_group:'Device Information',sorting_weight:1},
 {id:'button/Restart Gateway',name:'Restart Gateway',state:'',sorting_group:'Device Controls',sorting_weight:2}
];
const server=http.createServer(async(req,res)=>{
 const url=new URL(req.url,'http://127.0.0.1');requests.push(url.pathname);
 if(url.pathname==='/'||url.pathname==='/esphome'||url.pathname==='/esphome/'){
  res.setHeader('Content-Type','text/html');res.end('<!doctype html><html><head><meta charset="UTF-8"><link rel="icon" href="data:"></head><body><script type="module" src="/0.js"></script><esp-app></esp-app></body></html>');return;
 }
 if(url.pathname==='/0.js'){res.setHeader('Content-Type','text/javascript');res.end(bundle);return;}
 if(url.pathname==='/events'){
  res.writeHead(200,{'Content-Type':'text/event-stream','Connection':'keep-alive','Cache-Control':'no-store'});streams.add(res);res.on('close',()=>streams.delete(res));
  res.write('event: ping\ndata: '+JSON.stringify({title:'Legacy test',uptime:100,ota:false,log:true})+'\n\n');
  setTimeout(()=>{if(res.destroyed)return;for(const name of ['Device Information','Device Controls'])res.write('event: sorting_group\ndata: '+JSON.stringify({name,sorting_weight:1})+'\n\n');for(const entity of entities)res.write('event: state_detail_all\ndata: '+JSON.stringify(entity)+'\n\n');},120);return;
 }
 if(req.method==='GET'&&url.pathname.startsWith('/text_sensor/')){res.setHeader('Content-Type','application/json');res.end(JSON.stringify(entities[0]));return;}
 if(req.method==='GET'&&url.pathname.startsWith('/button/')){res.setHeader('Content-Type','application/json');res.end(JSON.stringify(entities[1]));return;}
 if(req.method==='POST'){actions.push(decodeURIComponent(url.pathname));res.end('OK');return;}
 res.writeHead(404);res.end('Not found');
});
(async()=>{
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const base='http://127.0.0.1:'+server.address().port;
 const browser=await chromium.launch({headless:true,executablePath:process.env.TZB_CHROMIUM_PATH||undefined});
 try{
  const page=await browser.newPage();const errors=[];page.on('pageerror',error=>{errors.push(error.message);console.log('PAGEERROR',error.stack);});page.on('console',m=>{if(m.type()==='error')console.log('CONSOLE',m.text());});
  for(const route of ['/esphome','/esphome/']){
   await page.goto(base+route);
   try{await page.getByText('ESP IP Address',{exact:true}).waitFor({timeout:4000});}catch(error){console.log('REQUESTS',requests);console.log('DOM',await page.locator('esp-app').evaluate(el=>({table:el.shadowRoot?.querySelector('esp-entity-table')?.shadowRoot?.innerHTML,entities:el.shadowRoot?.querySelector('esp-entity-table')?.entities})));throw error;}
   await page.getByText('192.168.1.51',{exact:true}).waitFor();
   await page.getByText('Restart Gateway',{exact:true}).waitFor();
   const pressResponse=page.waitForResponse(response=>response.request().method()==='POST');
   await page.getByRole('button',{name:'PRESS',exact:true}).click();
   await pressResponse;
   assert.equal(actions.at(-1),'/button/Restart Gateway/press');
   assert.equal(await page.getByRole('link',{name:'TubesZB frontend',exact:true}).getAttribute('href'),'/');
  }
  await page.goto(base+'/');
  await page.getByText('ESP IP Address',{exact:true}).waitFor();
  assert.equal(await page.getByRole('link',{name:'Network settings',exact:true}).getAttribute('href'),'/network');
  assert.equal(requests.some(route=>route.startsWith('/esphome/events')||route.startsWith('/esphome/button')||route.startsWith('/esphome/text_sensor')),false);
  assert.deepEqual(errors,[]);
  console.log('Actual minified ESPHome bundle passed: root SSE/REST paths, 2026.10 entity events, rendered controls, and return link.');
 }finally{await browser.close();for(const response of streams)response.end();await new Promise(resolve=>server.close(resolve));}
})().catch(error=>{console.error(error);process.exitCode=1;server.close();});
