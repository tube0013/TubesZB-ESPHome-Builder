const {componentsRoot}=require('./component_paths.cjs');
/* Browser checks against a mock device; hardware behavior is tested separately. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const {chromium} = require('playwright');

const root = path.resolve(__dirname, '..');
const component = path.join(componentsRoot, 'network_settings');
const html = fs.readFileSync(path.join(component, 'network_settings.html'), 'utf8')
  .replace('/* NETWORK_VALIDATION */', fs.readFileSync(path.join(component, 'network_validation.js'), 'utf8'));
const initial = () => ({mode:'dhcp', testing:false, remaining:120, restarting:false, storage_ok:true,
  nonce:'test-token', ip:'192.168.1.10', subnet:'255.255.255.0', gateway:'192.168.1.1', dns1:'192.168.1.1',
  dns2:'0.0.0.0', return_ip:'', hostname:'tubeszb.local', next_ip:'', trial_pending:false});
let state = initial(), mutations = [], rejectApply = false;
const server = http.createServer(async (req, res) => {
  res.setHeader('Cache-Control', 'no-store');
  if (req.method === 'GET' && req.url === '/network') {
    res.setHeader('Content-Type', 'text/html'); res.end(html); return;
  }
  res.setHeader('Content-Type', 'application/json');
  if (req.method === 'GET' && req.url === '/network/status') { res.end(JSON.stringify(state)); return; }
  let body = ''; for await (const chunk of req) body += chunk;
  mutations.push({url:req.url, body:Object.fromEntries(new URLSearchParams(body))});
  assert.equal(req.headers['x-tubeszb-network'], 'test-token');
  if (req.url === '/network/apply' && rejectApply) { res.writeHead(400); res.end('{"error":"Rejected test configuration"}'); return; }
  if (req.url === '/network/confirm') state.testing = false;
  res.end('{"ok":true}');
});

(async () => {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const url = `http://127.0.0.1:${server.address().port}/network`;
  const browser = await chromium.launch({headless:true, executablePath:process.env.TZB_CHROMIUM_PATH || undefined});
  try {
    const page = await browser.newPage({viewport:{width:1000,height:1100}});
    const scriptErrors = [];
    page.on('pageerror', error => scriptErrors.push(error.message));
    const load = async () => {
      await page.goto(url);
      await page.getByText(/DHCP ·|Static IP ·/).waitFor();
    };
    await load();
    await page.getByLabel('Address mode').selectOption('static');
    await page.getByLabel('IP address', {exact:true}).fill('999.168.1.50');
    await page.getByRole('button', {name:'Apply & Restart', exact:true}).click();
    assert.equal(mutations.length, 0);
    assert.match(await page.locator('#ip-error').textContent(), /four decimal/);
    await page.getByLabel('IP address', {exact:true}).fill('192.168.1.50');
    await page.getByLabel('Subnet mask').fill('255.0.255.0');
    assert.match(await page.locator('#subnet-error').textContent(), /contiguous/);
    await page.getByLabel('Subnet mask').fill('255.255.255.0');
    await page.getByLabel('Gateway', {exact:true}).fill('192.168.2.1');
    assert.match(await page.locator('#gateway-error').textContent(), /same subnet/);
    await page.getByLabel('Gateway', {exact:true}).fill('192.168.1.1');
    if (process.env.TZB_SCREENSHOT_DIR) {
      fs.mkdirSync(process.env.TZB_SCREENSHOT_DIR, {recursive:true});
      await page.screenshot({path:path.join(process.env.TZB_SCREENSHOT_DIR, 'network-settings.png'), fullPage:true});
    }
    await page.getByRole('button', {name:'Apply & Restart', exact:true}).click();
    await page.locator('#reconnect:not([hidden])').waitFor();
    assert.equal(mutations.length, 1);
    assert.equal(mutations[0].body.ip, '192.168.1.50');
    assert.match(await page.locator('#reconnect-location a').getAttribute('href'), /192\.168\.1\.50/);
    assert.equal(await page.locator('#apply').isDisabled(), true);

    // Pending configuration is only kept by an explicit click after reconnecting.
    state = {...initial(), mode:'static', testing:true, ip:'192.168.1.50'};
    await load();
    assert.equal(mutations.length, 1);
    await page.getByText('Automatic rollback in 120 seconds.', {exact:true}).waitFor();
    if (process.env.TZB_SCREENSHOT_DIR) {
      await page.screenshot({path:path.join(process.env.TZB_SCREENSHOT_DIR, 'network-trial.png'), fullPage:true});
      await page.setViewportSize({width:390,height:844});
      await page.screenshot({path:path.join(process.env.TZB_SCREENSHOT_DIR, 'network-mobile.png'), fullPage:true});
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
    }
    await page.getByRole('button', {name:'Keep settings', exact:true}).click();
    await page.getByText('Network settings confirmed and saved.', {exact:true}).waitFor();
    assert.equal(mutations.at(-1).url, '/network/confirm');
    assert.equal(await page.locator('#trial').isHidden(), true);
    assert.equal(await page.locator('#apply').isDisabled(), false);

    state = {...initial(), testing:true, return_ip:'192.168.1.20'};
    await load();
    await page.getByRole('button', {name:'Revert now', exact:true}).click();
    await page.locator('#reconnect:not([hidden])').waitFor();
    assert.match(await page.locator('#reconnect-location a').getAttribute('href'), /192\.168\.1\.20/);

    state = {...initial(), mode:'static', testing:true, remaining:0, return_ip:'192.168.1.20'};
    await load();
    await page.locator('#reconnect:not([hidden])').waitFor();
    assert.equal(await page.getByRole('button', {name:'Keep settings', exact:true}).isVisible(), false);

    // A page reloaded before the reboot still links to the staged candidate.
    state = {...initial(), restarting:true, trial_pending:true, next_ip:'192.168.1.80'};
    await load();
    await page.locator('#reconnect:not([hidden])').waitFor();
    assert.match(await page.locator('#reconnect-location a').getAttribute('href'), /192\.168\.1\.80/);

    state = initial(); rejectApply = true;
    await load();
    await page.getByRole('button', {name:'Use DHCP', exact:true}).click();
    await page.getByText(/Rejected test configuration/).waitFor();
    assert.equal(await page.locator('#reconnect').isHidden(), true);
    assert.equal(await page.locator('#apply').isDisabled(), false);

    state = {...initial(), storage_ok:false};
    await load();
    assert.equal(await page.locator('#apply').isDisabled(), true);
    assert.deepEqual(scriptErrors, []);
    console.log('Browser validation, apply, confirmation, revert, timeout, storage failure, and mobile layout checks passed.');
  } finally {
    await browser.close();
    await new Promise(resolve => server.close(resolve));
  }
})().catch(error => { console.error(error); process.exitCode = 1; server.close(); });
