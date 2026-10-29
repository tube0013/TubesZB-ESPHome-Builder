/* Host tests read the same external components that firmware imports from GitHub. */
const fs=require('node:fs');
const path=require('node:path');
const root=path.resolve(__dirname,'..');
const required=['network_settings','tubeszb_web','efr32_flasher','cc2652_flasher','zigbee_firmware_baud','cc2652_flash_guard'];
let candidates=process.env.TZB_COMPONENTS_ROOT?[process.env.TZB_COMPONENTS_ROOT]:[path.join(root,'.ci/esphome-components/components')];
if(!process.env.TZB_COMPONENTS_ROOT){
 const cache=path.join(root,'manifests/.esphome/external_components');
 if(fs.existsSync(cache)){
  const repos=fs.readdirSync(cache,{withFileTypes:true}).filter(p=>p.isDirectory()).map(p=>path.join(cache,p.name));
  const stamp=p=>fs.statSync(fs.existsSync(path.join(p,'.git/FETCH_HEAD'))?path.join(p,'.git/FETCH_HEAD'):p).mtimeMs;
  repos.sort((a,b)=>stamp(b)-stamp(a));candidates.push(...repos.map(p=>path.join(p,'components')));
 }
}
const componentsRoot=candidates.find(p=>required.every(name=>fs.existsSync(path.join(p,name,'__init__.py'))));
if(!componentsRoot)throw new Error('Set TZB_COMPONENTS_ROOT to an esphome-components checkout, or validate a manifest to populate its GitHub cache');
module.exports={componentsRoot:path.resolve(componentsRoot)};
