(() => {
  'use strict';
  const root = document.getElementById('tzb-app');
  const q = selector => root.querySelector(selector);
  const qa = selector => [...root.querySelectorAll(selector)];
  const titles = {overview:'Z-Wave gateway', zigbee:'Zigbee radio', zwave:'Z-Wave radio', network:'Network settings', firmware:'Firmware', diagnostics:'Diagnostics'};
  const fields = ['ip','subnet','gateway','dns1','dns2'];
  let page = new URL(location.href).searchParams.get('view') || 'overview';
  if (!titles[page]) page = 'overview';
  let theme = 'system', device = null, network = null, busy = false, polling = false, pollTimer = null;
  let formLoaded = false, trialDeadline = 0, restarting = false, modalAction = '', lastFocus = null;
  let logs = null, logRecords = [], logBytes = 0, droppedLines = 0, captureStarted = null, drawTimer = null;
  let logConnection = 'stopped', logStopReason = '';
  let uploading = false, firmwareCheckRequested = false;
  let noticeOwner = '', radioFlashRequested = false, radioFlashNotice = 'idle', radioFlashTimer = null;
  let radioNativeBaud = '', radioFormLoaded = false, gatewayInstallRequested = false, gatewayInstallNonce = '', uploadState = null;
  const gatewayActive = () => gatewayInstallRequested || device?.gateway_ota_state === 1 || (!device?.gateway_ota_state && device?.update_state === 3);
  const isCC2652 = () => device?.radio_type === 'cc2652';
  const isBasicRadio = () => device?.radio_type === 'serial';
  const isZigbee = () => device?.radio_type === 'efr32' || isCC2652() || isBasicRadio();
  const isDual = () => Boolean(device?.dual_radio);
  const hasZWave = () => Boolean(device?.zwave || (device && !isZigbee()));
  const hasRadioFirmware = () => isZigbee() && !isBasicRadio();
  const radioActive = () => Boolean(device?.zigbee?.busy || device?.zigbee?.result === 1);
  const MAX_LOG_BYTES = 1024 * 1024, MAX_LOG_LINES = 10000, MAX_VISIBLE_LINES = 250;
  try { theme = localStorage.getItem('tubeszb-appearance') || 'system'; } catch (_) {}
  if (!['system','light','dark'].includes(theme)) theme = 'system';
  const bind = (name,value) => qa('[data-bind="'+name+'"]').forEach(element => { element.textContent = value; });
  function clearRadioFlashNotice() {
    clearTimeout(radioFlashTimer);radioFlashTimer = null;radioFlashNotice = 'idle';
    q('[data-ui="radio-feedback"]').hidden = true;q('[data-ui="radio-progress"]').hidden = true;
    q('[data-ui="radio-banner-result"]').textContent = '';
    if (noticeOwner === 'radio-flash') {
      q('[data-ui="toast-message"]').textContent = '';
      q('[data-ui="toast"]').dataset.noticeError = 'false';noticeOwner = '';
    }
  }
  function notify(message, error = false, owner = '') {
    if (message && radioFlashNotice === 'complete' && owner !== 'radio-flash') clearRadioFlashNotice();
    noticeOwner = owner;
    const toast = q('[data-ui="toast"]');
    q('[data-ui="toast-message"]').textContent = message;
    toast.dataset.noticeError = String(error);
    renderFirmwareBanner();
  }
  function renderFirmwareBanner() {
    const toast = q('[data-ui="toast"]'), message = q('[data-ui="toast-message"]');
    const active = !q('[data-ui="gateway-feedback"]').hidden || !q('[data-ui="upload-feedback"]').hidden || !q('[data-ui="radio-feedback"]').hidden;
    const selected = !q('[data-ui="upload-feedback"]').hidden ? q('[data-ui="upload-result"]') : !q('[data-ui="radio-feedback"]').hidden ? q('[data-ui="radio-banner-result"]') : q('[data-ui="gateway-result"]');
    // Progress replaces the generic update notice; keep distinct errors visible.
    message.hidden = active && (toast.dataset.noticeError !== 'true' || message.textContent === selected.textContent);
    if (message.hidden && message.textContent === selected.textContent) message.textContent = '';
    toast.hidden = !active && !message.textContent;
    toast.classList.toggle('tzb-errornotice',toast.dataset.noticeError === 'true');
  }
  function appearance() {
    root.style.colorScheme = theme === 'system' ? 'light dark' : theme;
    document.documentElement.style.colorScheme = theme === 'system' ? 'light dark' : theme;
  }
  function addressURL(address) {
    const url = new URL('/', location.href);
    url.hostname = address;
    url.searchParams.set('view','network');
    return url.href;
  }
  function makeLink(element, address) {
    element.replaceChildren();
    const anchor = document.createElement('a');
    anchor.href = addressURL(address);
    anchor.textContent = anchor.href;
    anchor.style.color = 'var(--tzb-blue)';
    element.append(anchor);
  }
  async function request(path, data, timeout = 7000) {
    const options = {cache:'no-store', signal:AbortSignal.timeout(timeout)};
    if (data !== undefined) {
      const token = path.startsWith('/network/') ? network?.nonce : device?.nonce;
      if (!token) throw new Error('Wait for the device to connect before making changes.');
      options.method = 'POST';
      options.headers = {'Content-Type':'application/x-www-form-urlencoded',
        [path.startsWith('/network/') ? 'X-TubesZB-Network' : 'X-TubesZB-UI']:token};
      options.body = new URLSearchParams(data);
    }
    const response = await fetch(path, options);
    const body = await response.json();
    if (!response.ok) {
      const error = new Error(body.error || 'The device could not complete the request (HTTP '+response.status+').');
      error.httpStatus = response.status;
      throw error;
    }
    return body;
  }
  function isGBLURL(value) {
    try {
      const url = new URL(value);
      return /^https?:\/\//.test(value) && Boolean(url.hostname) && value.length <= 255 && !/\s/.test(value) && /\.gbl$/i.test(url.pathname);
    } catch (_) { return false; }
  }
  function formValues() { return Object.fromEntries(['mode',...fields].map(key => [key,q('[name="'+key+'"]').value])); }
  function modeChanged() {
    const isStatic = q('[name="mode"]').value === 'static';
    q('[data-ui="static-fields"]').hidden = !isStatic;
    q('[data-ui="dhcp-help"]').hidden = isStatic;
  }
  function fillForm() {
    if (!network) return;
    q('[name="mode"]').value = network.mode;
    for (const field of fields) q('[name="'+field+'"]').value = network[field] === '0.0.0.0' ? '' : network[field];
    modeChanged();
    formLoaded = true;
  }
  function validateForm() {
    const errors = TZBNetworkValidation.validate(formValues());
    qa('[data-error]').forEach(element => {
      const key = element.dataset.error;
      element.textContent = errors[key] || '';
      q('[name="'+key+'"]').setAttribute('aria-invalid',String(Boolean(errors[key])));
    });
    return errors;
  }
  function controls() {
    q('[data-action="logs"]').disabled = uploading || restarting;
    const disabled = !device || !network || busy || restarting || uploading || device.action_pending || gatewayActive() || radioActive();
    q('[data-ui="network-fieldset"]').disabled = disabled || network.testing || !network.storage_ok;
    q('[data-ui="offline-switch"]').disabled = disabled || Boolean(network?.testing) || !network?.storage_ok || Boolean(device?.checking);
    for (const action of ['keep','revert']) q('[data-action="'+action+'"]').disabled = disabled || !network.testing || !network.storage_ok;
    for (const action of ['check-update','install-update','choose-file','review-file']) {
      q('[data-action="'+action+'"]').disabled = disabled || Boolean(network?.testing) || Boolean(device?.checking) || device?.update_state === 3;
    }
    for (const action of ['check-update','install-update']) q('[data-action="'+action+'"]').disabled ||= Boolean(network?.use_without_internet);
    qa('[data-action="restart"]').forEach(element => { element.disabled = disabled || device?.update_state === 3; });
    qa('[data-channel]').forEach(element => { element.disabled = disabled || Boolean(network?.testing) || device?.checking || (element.dataset.channel === "zigbee" && device?.zigbee?.supports_zigbee_beta === false); });
    for (const action of ['radio-check','radio-flash','radio-probe','radio-reset','serial-pause','serial-resume','radio-baud','radio-url-save','radio-url-clear','radio-bootloader']) {
      q('[data-action="'+action+'"]').disabled = disabled || Boolean(network?.testing) || Boolean(device?.checking);
    }
    for (const action of ['radio-check','radio-flash']) q('[data-action="'+action+'"]').disabled ||= Boolean(network?.use_without_internet);
    if (isBasicRadio()) {
      q('[data-action="radio-baud"]').disabled ||= !device?.zigbee?.baud_options?.length;
      for (const action of ['radio-check','radio-flash','radio-probe','radio-bootloader']) q('[data-action="'+action+'"]').disabled = true;
      qa('[data-channel]').forEach(element => { element.disabled = true; });
    }
    if (isZigbee()) {
      const radio = device.zigbee;
      const direct = isGBLURL(radio.custom_url || '');
      q('[data-action="radio-flash"]').disabled ||= radio.custom_url ? !direct : !radio.checked;
      q('[data-action="radio-check"]').disabled ||= Boolean(radio.custom_url);
      for (const action of ['radio-baud','radio-url-save','radio-url-clear']) q('[data-action="'+action+'"]').disabled ||= isCC2652();
      q('[data-action="serial-pause"]').disabled ||= radio.paused;
      q('[data-action="serial-resume"]').disabled ||= !radio.paused;
    }
  }
  function uptime(seconds) {
    const days = Math.floor(seconds / 86400), hours = Math.floor(seconds % 86400 / 3600), minutes = Math.floor(seconds % 3600 / 60);
    return (days ? days+'d ' : '') + hours+'h '+minutes+'m';
  }
  function render() {
    qa('[data-zigbee]').forEach(element => { element.hidden = !isZigbee(); });
    qa('[data-efr32]').forEach(element => { element.hidden = device?.radio_type !== 'efr32'; });
    qa('[data-cc2652]').forEach(element => { element.hidden = !isCC2652(); });
    qa('[data-zwave-only]').forEach(element => { element.hidden = !hasZWave(); });
    qa('[data-dual]').forEach(element => { element.hidden = !isDual(); });
    qa('[data-radio-firmware]').forEach(element => { element.hidden = !hasRadioFirmware(); });
    qa('[data-basic-radio]').forEach(element => { element.hidden = !isBasicRadio(); });
    qa('[data-serial-settings]').forEach(element => { element.hidden = !isZigbee() || isCC2652() || (isBasicRadio() && !device?.zigbee?.baud_options?.length); });
    if (device && !isZigbee() && page === 'zigbee') page = 'overview';
    if (device && !hasZWave() && page === 'zwave') page = 'overview';
    titles.overview = isDual() ? 'Dual-radio gateway' : isZigbee() ? 'Zigbee gateway' : 'Z-Wave gateway';
    qa('[data-view]').forEach(element => { element.hidden = element.dataset.view !== page; });
    q('[data-ui="log-panel"]').hidden = page === 'overview';
    qa('.tzb-nav[data-page]').forEach(element => {
      if (element.dataset.page === page) element.setAttribute('aria-current','page');
      else element.removeAttribute('aria-current');
    });
    bind('heading',titles[page]);
    bind('breadcrumb',page[0].toUpperCase()+page.slice(1));
    if (device) {
      document.title = device.model+' · TubesZB';
      bind('name',device.name);bind('model',device.model);bind('mac',device.mac);
      bind('board',device.board || 'ESP32');
      bind('radio-label',isDual() ? 'DUAL RADIO GATEWAY' : isZigbee() ? 'ZIGBEE GATEWAY' : 'Z-WAVE GATEWAY');
      bind('proxy-panel-title',isZigbee() ? 'Zigbee serial server' : 'Z-Wave proxy');
      bind('transport',isZigbee() ? 'TCP serial · 6638' : 'ESPHome API');
      bind('project-version',device.project_version);bind('esphome-version',device.esphome_version);
      bind('firmware-description','ESPHome '+device.esphome_version+' · '+device.model);
      bind('baud',String(device.baud_rate));bind('ethernet-state',device.ethernet_connected ? 'Connected' : 'Disconnected');
      bind('proxy-state',device.proxy_connected ? 'Connected' : 'Waiting');
      bind('proxy-title',device.proxy_connected ? 'Z-Wave client connected' : 'Waiting for a Z-Wave client');
      bind('proxy-copy',device.proxy_connected ? 'The client has subscribed to this gateway’s Z-Wave proxy.' : 'Connect this gateway through the ESPHome integration to use its Z-Wave radio.');
      q('[data-bind="proxy-state"]').closest('.tzb-status').classList.toggle('tzb-neutral',!device.proxy_connected);
      bind('uptime',uptime(device.uptime));
      bind('free-heap',Math.round(device.free_heap/1024)+' KiB');
      const zwave = device.zwave || {connected:device.proxy_connected,home_id:device.home_id,baud_rate:device.baud_rate};
      bind('home-id',zwave.home_id ? '0x'+zwave.home_id.toString(16).padStart(8,'0').toUpperCase() : 'Not reported');
      bind('zwave-state',zwave.connected ? 'Connected' : 'Waiting');
      bind('zwave-title',zwave.connected ? 'Z-Wave client connected' : 'Waiting for a Z-Wave client');
      bind('zwave-copy',zwave.connected ? 'The client has subscribed to this gateway’s Z-Wave proxy.' : 'Connect this gateway through the ESPHome integration to use its Z-Wave radio.');
      bind('zwave-baud',String(zwave.baud_rate));
      qa('[data-bind="zwave-state"]').forEach(element => element.closest('.tzb-status')?.classList.toggle('tzb-neutral',!zwave.connected));
      let result = 'Updates have not been checked.';
      const otaState = device.gateway_ota_state || 0;
      const percent = Number.isFinite(device.gateway_ota_progress) && device.gateway_ota_progress >= 0 ? Math.min(100,Math.floor(device.gateway_ota_progress)) : null;
      if (gatewayActive()) result = percent === null ? 'Firmware installation requested. Do not disconnect power.' : 'Installing gateway firmware: '+percent+'% — do not disconnect power.';
      else if (otaState === 2) result = 'Firmware verified. The gateway is restarting.';
      else if (otaState === 3) result = 'Firmware installation failed or was aborted'+(device.gateway_ota_error ? ' (OTA error '+device.gateway_ota_error+')' : '')+'. Open Diagnostics for details.';
      else if (device.checking || firmwareCheckRequested) result = 'Checking the release manifest…';
      else if (device.update_error) result = 'Could not check or install firmware. Check your connection or open the device logs.';
      else if (device.update_verified === false) result = 'Check the selected gateway channel before installing.';
      else if (device.update_state === 1) result = 'You’re up to date.';
      else if (device.update_state === 2) result = 'Update available: '+device.latest_version;
      const progress = q('[data-ui="gateway-progress"]');
      progress.hidden = q('[data-ui="gateway-feedback"]').hidden = !gatewayActive() && otaState === 0;
      q('[data-ui="gateway-result"]').textContent = progress.hidden ? '' : result;
      if (percent === null) progress.removeAttribute('value');else progress.value = percent;
      q('[data-ui="update-result"]').textContent = progress.hidden ? result : '';
      q('[data-ui="update-label"]').textContent = device.checking || firmwareCheckRequested ? 'Checking…' : 'Check for updates';
      q('[data-ui="install-update"]').hidden = device.update_state !== 2 || device.update_error || device.checking || firmwareCheckRequested || device.update_verified === false;
      if (isZigbee()) renderRadio();
    }
    if (network) {
      bind('ip',network.ip);bind('gateway',network.gateway);bind('mode',network.mode === 'static' ? 'Static IP' : 'DHCP');
      bind('trial-ip',network.ip);
      q('[data-ui="trial"]').hidden = !network.testing || restarting;
      if (network.testing) makeLink(q('[data-ui="return-location"]'),network.return_ip || network.hostname);
    }
    q('[data-ui="offline-switch"]').checked = Boolean(network?.use_without_internet);
    q('[data-ui="offline-reminder"]').hidden = !network?.use_without_internet;
    if (network?.use_without_internet && !gatewayActive()) {
      q('[data-ui="update-result"]').textContent = 'Firmware checks are disabled while using without internet.';
      q('[data-ui="install-update"]').hidden = true;
      if (isZigbee() && !radioActive()) q('[data-ui="radio-result"]').textContent = 'Firmware checks and downloads are disabled while using without internet.';
    }
    renderUploadProgress();renderLogs();controls();
  }
  function navigate(next) {
    if (!titles[next]) return;
    page = next;
    const url = new URL(location.href);url.searchParams.set('view',page);history.replaceState(null,'',url);
    notify('');render();
    if (q('[data-ui="log-panel"]').open && page !== 'overview') q('[data-ui="logs"]').scrollTop = q('[data-ui="logs"]').scrollHeight;
    if (page === 'network' && !formLoaded) fillForm();
    schedulePoll(0);
  }
  function schedulePoll(delay) {
    clearTimeout(pollTimer);
    if (!restarting && !uploading && !document.hidden) pollTimer = setTimeout(refresh,delay);
  }
  async function refresh() {
    if (polling || busy || restarting || uploading) { schedulePoll(1000); return; }
    polling = true;
    try {
      const [nextDevice,nextNetwork] = await Promise.all([request('/tubeszb/status'),request('/network/status')]);
      if (gatewayInstallNonce && nextDevice.nonce !== gatewayInstallNonce) {
        gatewayInstallRequested = false;gatewayInstallNonce = '';
        notify('Gateway restarted. Reported firmware: '+nextDevice.project_version+'.');
      }
      if (nextDevice.gateway_ota_state > 0) gatewayInstallRequested = false;
      if (nextDevice.gateway_ota_state === 1 && !gatewayInstallNonce) gatewayInstallNonce = nextDevice.nonce;
      if (radioFlashRequested && !nextDevice.action_pending) radioFlashRequested = false;
      device = nextDevice;network = nextNetwork;
      firmwareCheckRequested = false;
      bind('online','Device online');q('[data-bind="online"]').closest('.tzb-status').classList.remove('tzb-neutral');
      if (!formLoaded) fillForm();
      if (network.testing) trialDeadline = performance.now()+network.remaining*1000;
      if (!network.storage_ok) notify('Network settings storage is unavailable. Restart the device before making changes.',true);
      if (network.restarting) showReconnect(network.next_ip || network.hostname,network.trial_pending);
      render();
      if (device.gateway_ota_state === 2) showReconnect(network.ip,false);
    } catch (error) {
      bind('online','Connection lost');q('[data-bind="online"]').closest('.tzb-status').classList.add('tzb-neutral');
      notify(gatewayActive() ? 'Connection interrupted during firmware installation. Completion is not yet confirmed; waiting for the gateway to return.' : 'Cannot reach the gateway. '+error.message,true,radioFlashNotice === 'active' ? 'radio-flash' : '');
    } finally {
      polling = false;
      schedulePoll(gatewayActive() ? 1000 : network?.testing || device?.checking || radioActive() || page === 'firmware' || page === 'zigbee' ? 2000 : 10000);
    }
  }
  function showReconnect(address, trial) {
    restarting = true;clearTimeout(pollTimer);stopLogs('Capture stopped while the gateway restarts. Captured logs are retained.');
    q('[data-ui="trial"]').hidden = true;
    q('[data-ui="reconnect"]').hidden = false;
    q('[data-ui="reconnect-copy"]').textContent = trial ? 'Wait a few seconds, then open the new address and select Keep settings within two minutes.' : 'The gateway is restarting. Wait a few seconds, then reconnect.';
    makeLink(q('[data-ui="reconnect-link"]'),address);
    const fallback = network?.return_ip || network?.hostname || device?.name+'.local';
    q('[data-ui="reconnect-fallback"]').textContent = trial ? 'Without confirmation, the gateway restores '+fallback+'. For DHCP, check your router’s lease list if the hostname does not resolve.' : 'Confirmed network settings are retained. A DHCP address may change after restart.';
    bind('online','Restarting');controls();
  }
  async function applyNetwork(values) {
    if (busy || restarting || !network) return;
    if (values.mode === 'static') {
      const errors = validateForm();
      if (Object.keys(errors).length) { q('[name="'+Object.keys(errors)[0]+'"]').focus();return; }
    }
    busy = true;controls();notify('Saving trial network settings…');
    const target = values.mode === 'static' ? values.ip : network.hostname;
    try {
      await request('/network/apply',values);
      showReconnect(target,true);notify('Trial saved. Reconnect to keep these settings.');
    } catch (error) {
      if (!error.httpStatus) {
        showReconnect(target,true);
        notify('The response was interrupted. The settings may have been applied; open the new address to check for a trial.',true);
      } else notify(error.message,true);
    } finally { busy = false;controls(); }
  }
  async function finishTrial(keep) {
    if (busy || restarting) return;
    busy = true;controls();
    try {
      await request(keep ? '/network/confirm' : '/network/revert',{});
      if (keep) { network.testing = false;formLoaded = false;notify('Network settings confirmed and saved.'); }
      else showReconnect(network.return_ip || network.hostname,false);
    } catch (error) { notify(error.message,true); }
    finally { busy = false;render();if (!restarting) schedulePoll(0); }
  }
  function openModal(action,title,copy,label) {
    lastFocus = document.activeElement;modalAction = action;
    q('[data-ui="dialog-release"]').hidden = action !== 'internet:on';
    q('#tzb-dialog-title').textContent = title;q('[data-ui="dialog-copy"]').textContent = copy;
    q('[data-action="confirm-modal"]').textContent = label;q('[data-ui="modal"]').hidden = false;
    q('[data-action="cancel-modal"]').focus();
  }
  function closeModal() {
    q('[data-ui="modal"]').hidden = true;modalAction = '';
    if (lastFocus && root.contains(lastFocus)) lastFocus.focus();
  }
  async function setInternetUse(offline) {
    if (busy || restarting || uploading || !network) return;
    busy = true;controls();
    try {
      await request('/network/internet',{use_without_internet:String(offline)});
      network = await request('/network/status');
      notify(offline ? 'Use without internet enabled and saved.' : 'Internet checks enabled and saved.');
    } catch (error) { notify(error.message,true); }
    finally { busy = false;render();schedulePoll(0); }
  }
  async function deviceAction(action) {
    if (busy || restarting || !device) return;
    busy = true;controls();
    try {
      await request('/tubeszb/'+action,{});
      if (action === 'restart') showReconnect(network.testing ? (network.return_ip || network.hostname) : network.ip,false);
      else {
        device.action_pending = true;
        if (action === 'check-update' || action === 'install-update') uploadState = null;
        if (action === 'check-update') firmwareCheckRequested = true;
        if (action === 'install-update') { gatewayInstallRequested = true;gatewayInstallNonce = device.nonce;device.gateway_ota_progress = -1;device.gateway_ota_state = 0; }
        notify(action === 'check-update' ? 'Checking for firmware updates…' : 'Firmware installation requested. Do not disconnect power.');
      }
    } catch (error) { notify(error.message,true); }
    finally { busy = false;render();schedulePoll(1000); }
  }
  function renderRadio() {
    const radio = device.zigbee;
    bind('radio-board-label',isBasicRadio() ? 'Radio model' : isCC2652() ? 'Radio variant' : 'Radio board');
    q('[data-action="radio-probe"]').textContent = isCC2652() ? 'Detect CC2652 variant' : 'Probe radio info';
    bind('proxy-state',radio.busy || radio.result === 1 ? 'Busy' : radio.paused ? 'Paused' : radio.connected ? 'Connected' : 'Waiting');
    bind('proxy-title',radio.busy || radio.result === 1 ? 'Zigbee operation in progress' : radio.paused ? 'Serial server paused' : radio.connected ? 'Zigbee client connected' : 'Waiting for a Zigbee client');
    bind('proxy-copy','Connect ZHA or Zigbee2MQTT to tcp://'+network.ip+':'+radio.port+'.');
    q('[data-bind="proxy-state"]').closest('.tzb-status').classList.toggle('tzb-neutral',!radio.connected || radio.paused || radio.busy);
    for (const [field,key] of [['radio-firmware','firmware'],['radio-latest','latest'],['radio-board','board_name'],['radio-manufacturer','manufacturer'],['radio-ieee','ieee'],['radio-info-status','info_status']]) bind(field,radio[key] || 'Not reported');
    if (radio.custom_url) bind('radio-latest',isGBLURL(radio.custom_url) ? 'Custom image' : 'Invalid custom URL');
    else if (!radio.checked) bind('radio-latest','Not checked for this source');
    bind('radio-busy-label',radio.busy || radio.result === 1 ? 'Busy' : 'Idle');
    bind('radio-client',radio.connected ? 'Connected' : 'Not connected');
    bind('radio-serial-status',radio.paused ? 'Paused' : 'Running');bind('radio-port',String(radio.port));
    bind('update-channel',radio.gateway_beta ? 'Beta gateway channel' : 'Stable gateway channel');
    bind('zigbee-channel',radio.zigbee_beta ? 'Beta Zigbee channel' : 'Stable Zigbee channel');
    q('[data-channel="gateway"]').checked = radio.gateway_beta;
    q('[data-channel="zigbee"]').checked = radio.zigbee_beta;
    q('[data-channel="zigbee"]').closest("label").hidden = radio.supports_zigbee_beta === false;
    const source = radio.custom_url ? 'Custom source: '+radio.custom_url : (radio.zigbee_beta ? 'Source: beta release manifest' : 'Source: stable release manifest');
    q('[data-ui="radio-source"]').textContent = source;
    if (!radioFormLoaded) {
      const baud = q('#tzb-radio-baud');baud.replaceChildren();
      for (const value of radio.baud_options || []) { const option = document.createElement('option');option.value = value;option.textContent = value;baud.append(option); }
      baud.value = radio.baud;radioNativeBaud = radio.baud;q('#tzb-radio-url').value = radio.custom_url;radioFormLoaded = true;
    }
    if (radio.baud && radio.baud !== radioNativeBaud) {
      q('#tzb-radio-baud').value = radio.baud;radioNativeBaud = radio.baud;
    }
    let result = radio.custom_url ? (isGBLURL(radio.custom_url) ? 'Custom .gbl image selected. Review and flash it directly.' : 'Enter a direct .gbl file URL or use the selected channel.') : radio.checked ? 'Selected firmware source checked.' : 'Check the selected channel before flashing.';
    if (radio.result === 1) result = radio.operation === 'flash' ? (radio.progress >= 0 ? 'Flashing Zigbee radio: '+radio.progress+'% — do not disconnect power.' : 'Preparing the Zigbee radio flasher…') : radio.operation === 'check' ? 'Checking Zigbee firmware…' : 'Applying radio settings or querying the radio…';
    else if (radio.result === 3) result = 'Zigbee operation failed. Open Diagnostics for details.';
    else if (radio.result === 2 && radio.operation === 'flash') result = isCC2652() ? 'CC2652 firmware transfer completed. Installed radio info refreshes automatically.' : 'Radio firmware transfer completed. Probe radio info to refresh the installed version.';
    else if (radio.result === 2 && radio.operation === 'bootloader') result = 'Manual bootloader ready. Use an external flasher, or reset the radio to resume normal operation.';
    q('[data-ui="radio-result"]').textContent = result;
    renderRadioFlashBanner(radio);
  }
  function renderRadioFlashBanner(radio) {
    const running = radioFlashRequested || (radio.operation === 'flash' && (radio.result === 1 || radio.busy));
    if (running) {
      clearTimeout(radioFlashTimer);radioFlashTimer = null;radioFlashNotice = 'active';
    } else if ((radioFlashNotice === 'active' || radioFlashNotice === 'failed') && radio.operation === 'flash' && radio.result === 2) {
      radioFlashNotice = 'complete';
      if (noticeOwner === 'radio-flash') { q('[data-ui="toast-message"]').textContent = '';q('[data-ui="toast"]').dataset.noticeError = 'false';noticeOwner = ''; }
      radioFlashTimer = setTimeout(() => { clearRadioFlashNotice();render(); },5000);
    } else if (radioFlashNotice === 'active') {
      radioFlashNotice = 'failed';
      notify(radio.result === 3 ? 'Firmware flash failed. Open Diagnostics for details.' : 'Firmware flash completion was not confirmed. Check the radio status.',true,'radio-flash-result');
    }
    const visible = radioFlashNotice === 'active' || radioFlashNotice === 'complete';
    q('[data-ui="radio-feedback"]').hidden = !visible;
    const progress = q('[data-ui="radio-progress"]');progress.hidden = !visible;
    const value = radioFlashNotice === 'complete' ? 100 : radioFlashRequested ? -1 : radio.progress;
    if (value >= 0) progress.value = value;else progress.removeAttribute('value');
    q('[data-ui="radio-banner-result"]').textContent = !visible ? '' : radioFlashNotice === 'complete' ? 'Firmware flash complete.' : value >= 0 ? 'Flashing Zigbee radio: '+value+'% — do not disconnect power.' : 'Preparing the Zigbee radio flasher…';
    q('[data-ui="radio-result"]').hidden = radioFlashNotice === 'active';
  }
  async function zigbeeAction(action, value = '') {
    if (busy || restarting || uploading || !isZigbee()) return;
    busy = true;controls();
    try {
      await request('/tubeszb/zigbee-action',{action,value});
      device.action_pending = true;
      if (action === 'flash') { radioFlashRequested = true;uploadState = null;clearRadioFlashNotice();radioFlashNotice = 'active'; }
      if (action === 'gateway-channel') { device.update_verified = false;device.zigbee.gateway_beta = value === 'beta'; }
      if (action === 'zigbee-channel' || action === 'url' || action === 'check') device.zigbee.checked = false;
      if (action === 'zigbee-channel') device.zigbee.zigbee_beta = value === 'beta';
      if (action === 'url') device.zigbee.custom_url = value;
      notify(action === 'flash' ? 'Zigbee flash requested. Keep power connected until completion.' : action.endsWith('channel') ? (action === 'gateway-channel' ? 'Gateway' : 'Zigbee')+' firmware channel set to '+value+'. Check for updates before installing.' : 'Zigbee action requested.',false,action === 'flash' ? 'radio-flash' : '');
    } catch (error) { notify(error.message,true); }
    finally { busy = false;render();schedulePoll(1000); }
  }
  function renderLogs() {
    const box = q('[data-ui="logs"]');
    const nearBottom = box.scrollHeight-box.scrollTop-box.clientHeight < 45;
    box.textContent = logRecords.slice(-MAX_VISIBLE_LINES).join('\n');
    box.hidden = !logRecords.length;
    const state = !logs ? 'Stopped' : logConnection === 'connected' ? 'Capturing' : logConnection === 'reconnecting' ? 'Reconnecting…' : 'Connecting…';
    q('[data-ui="log-badge"]').textContent = state;
    q('[data-ui="log-badge"]').classList.toggle('tzb-live',Boolean(logs) && logConnection === 'connected');
    q('[data-ui="logs-idle"]').hidden = Boolean(logRecords.length);
    q('[data-ui="logs-idle"]').textContent = logs ? 'Waiting for log messages…' : 'No logs captured. Start capture to view device logs.';
    q('[data-action="logs"]').textContent = logs ? 'Stop capture' : 'Start capture';
    q('[data-action="logs"]').disabled = uploading || restarting;
    q('[data-action="download-logs"]').disabled = !logRecords.length;
    q('[data-action="clear-logs"]').disabled = !logRecords.length;
    q('[data-ui="log-summary"]').textContent = state+' · '+logRecords.length+' lines'+(droppedLines ? ' · '+droppedLines+' older lines discarded' : '')+' · browser buffer';
    q('[data-ui="log-notice"]').hidden = !logStopReason;
    q('[data-ui="log-notice"]').textContent = logStopReason;
    if (nearBottom) box.scrollTop = box.scrollHeight;
  }
  function appendLog(text) {
    const clean = text.replace(/\x1b\[[0-?]*[ -/]*[@-~]/g,'').replace(/\r/g,'');
    const timestamp = new Date().toISOString();
    for (const line of clean.split('\n')) {
      if (!line) continue;
      const record = timestamp+' '+line.slice(0,4096);
      logRecords.push(record);logBytes += record.length;
    }
    while (logRecords.length > MAX_LOG_LINES || logBytes > MAX_LOG_BYTES) {
      logBytes -= logRecords.shift().length;droppedLines++;
    }
    if (!drawTimer) drawTimer = setTimeout(()=>{drawTimer = null;renderLogs();},200);
  }
  function startLogs() {
    if (logs || page === 'overview' || uploading || restarting) return;
    if (!captureStarted) captureStarted = new Date().toISOString();
    logConnection = 'connecting';logStopReason = '';
    const source = new EventSource('/events');logs = source;
    source.addEventListener('log',event=>{if (logs === source) appendLog(event.data);});
    source.onopen = ()=>{if (logs === source) {logConnection = 'connected';logStopReason = '';renderLogs();}};
    source.onerror = ()=>{
      if (logs !== source) return;
      if (source.readyState === EventSource.CLOSED) {
        source.close();logs = null;logConnection = 'stopped';logStopReason = 'Log connection closed. Start capture to reconnect.';
      } else logConnection = 'reconnecting';
      renderLogs();
    };
    renderLogs();
  }
  function stopLogs(reason = '') {
    const source = logs;logs = null;
    if (source) source.close();
    logConnection = 'stopped';logStopReason = reason;renderLogs();
  }
  function downloadLogs() {
    if (!logRecords.length) return;
    const header = ['TubesZB device log','Device: '+(device?.name || 'unknown'),'Model: '+(device?.model || 'unknown'),
      'Firmware: '+(device?.project_version || 'unknown')+' / ESPHome '+(device?.esphome_version || 'unknown'),
      'Capture started: '+captureStarted,'Exported: '+new Date().toISOString(),
      'Retained lines: '+logRecords.length+'; discarded older lines: '+droppedLines,''].join('\n');
    const url = URL.createObjectURL(new Blob([header+'\n'+logRecords.join('\n')+'\n'],{type:'text/plain;charset=utf-8'}));
    const link = document.createElement('a');link.href = url;
    link.download = (device?.name || 'tubeszb')+'-logs-'+new Date().toISOString().replace(/[:.]/g,'-')+'.txt';
    root.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);
  }
  function supportDetails() {
    q('[data-ui="support"]').hidden = false;
    q('[data-ui="support"]').textContent = [device.model,'Device: '+device.name,
      'Firmware: '+device.project_version+' / ESPHome '+device.esphome_version,'IP: '+network.ip+' ('+network.mode+')',
      'MAC: '+device.mac,(isZigbee() ? 'Zigbee TCP client: ' : 'Proxy client: ')+(device.proxy_connected ? 'connected' : 'not connected'),
      'Uptime: '+uptime(device.uptime),'Free internal heap: '+device.free_heap+' bytes',
      ...(isDual() ? ['Z-Wave client: '+(device.zwave.connected ? 'connected' : 'not connected'),'Z-Wave Home ID: '+(device.zwave.home_id || 'not reported'),'Z-Wave baud: '+device.zwave.baud_rate] : []),
      ...(isZigbee() ? ['Zigbee firmware: '+device.zigbee.firmware,'Zigbee board: '+device.zigbee.board_name,
        'Zigbee IEEE: '+device.zigbee.ieee,'Gateway beta: '+device.zigbee.gateway_beta,'Zigbee beta: '+device.zigbee.zigbee_beta] : [])].join('\n');
  }
  function renderUploadProgress() {
    const progress = q('[data-ui="upload-progress"]'), result = q('[data-ui="upload-result"]');
    progress.hidden = q('[data-ui="upload-feedback"]').hidden = !uploadState;
    if (uploadState) {
      q('[data-ui="gateway-feedback"]').hidden = true;q('[data-ui="gateway-progress"]').hidden = true;
      result.textContent = uploadState.text;
      if (uploadState.progress === null) progress.removeAttribute('value');else progress.value = uploadState.progress;
    }
    q('[data-ui="update-result"]').hidden = Boolean(uploadState) || !q('[data-ui="gateway-feedback"]').hidden;
    renderFirmwareBanner();
  }
  async function uploadFirmware() {
    const file = q('#tzb-firmware-file').files[0];
    if (!file || network?.testing || busy || uploading || gatewayActive()) return;
    if (!file.name.endsWith('.ota.bin')) { notify('Choose an .ota.bin image for this device. Factory images must be flashed over USB.',true);return; }
    uploading = true;clearTimeout(pollTimer);stopLogs('Capture stopped for gateway firmware upload. Captured logs are retained.');controls();
    uploadState = {progress:0,text:'Uploading gateway firmware: 0% — do not disconnect power.'};renderUploadProgress();
    notify('Uploading firmware. Do not disconnect power or close this page.');
    try {
      const body = new FormData();body.append('update',file,file.name);
      const response = await new Promise((resolve,reject) => {
        const xhr = new XMLHttpRequest();xhr.open('POST','/update');xhr.timeout = 180000;
        xhr.upload.onprogress = event => {
          uploadState.progress = event.lengthComputable ? Math.min(100,Math.floor(event.loaded/event.total*100)) : null;
          uploadState.text = uploadState.progress === 100 ? 'Upload sent. Waiting for device verification — keep power connected.' : 'Uploading gateway firmware'+(uploadState.progress === null ? '…' : ': '+uploadState.progress+'%')+' — do not disconnect power.';
          renderUploadProgress();
        };
        xhr.onload = () => resolve({ok:xhr.status >= 200 && xhr.status < 300,text:xhr.responseText});
        xhr.onerror = () => reject(new Error('The connection was interrupted.'));
        xhr.ontimeout = () => reject(new Error('The firmware upload timed out.'));
        xhr.onabort = () => reject(new Error('The firmware upload was aborted.'));
        xhr.send(body);
      });
      if (!response.ok || !response.text.includes('Update Successful')) throw new Error(response.text || 'Firmware upload failed.');
      uploadState = {progress:100,text:'Firmware uploaded successfully. The gateway is restarting.'};renderUploadProgress();
      showReconnect(network.ip,false);notify('Firmware uploaded successfully. The gateway is restarting.');
    } catch (error) {
      uploadState.text = 'Upload was not confirmed: '+error.message+' Check whether the device restarted before trying again.';renderUploadProgress();
      notify(uploadState.text,true);
    } finally { uploading = false;controls();if (!restarting) schedulePoll(3000); }
  }
  root.addEventListener('click',event=>{
    const button = event.target.closest('button');if (!button || button.disabled) return;
    if (button.dataset.page) { navigate(button.dataset.page);return; }
    switch (button.dataset.action) {
      case 'theme':
        theme = (theme === 'dark' || (theme === 'system' && matchMedia('(prefers-color-scheme:dark)').matches)) ? 'light' : 'dark';
        appearance();try { localStorage.setItem('tubeszb-appearance',theme); } catch (_) {}break;
      case 'apply':applyNetwork(formValues());break;
      case 'dhcp':q('[name="mode"]').value = 'dhcp';modeChanged();applyNetwork({mode:'dhcp'});break;
      case 'keep':finishTrial(true);break;
      case 'revert':finishTrial(false);break;
      case 'legacy':location.assign('/esphome');break;
      case 'docs':window.open('https://tubeszb.com/documentation','_blank','noopener,noreferrer');break;
      case 'restart':openModal('restart','Restart this gateway?',network?.testing ? 'Restarting now will revert the unconfirmed network trial.' : 'The radio connection will briefly disconnect while the gateway restarts.','Restart device');break;
      case 'radio-firmware':
        navigate('firmware');
        q('[data-ui="zigbee-firmware"]').scrollIntoView({block:'start'});
        q('[data-ui="zigbee-firmware"] h2').focus({preventScroll:true});
        break;
      case 'radio-check':zigbeeAction('check');break;
      case 'radio-flash':openModal('radio:flash','Flash Zigbee radio firmware?',(device.zigbee.custom_url || (device.zigbee.zigbee_beta ? 'Beta Zigbee channel' : 'Stable Zigbee channel'))+' · Radio: '+(device.zigbee.board_name || 'not reported')+'. This interrupts the Zigbee serial connection.'+(isDual() ? ' Z-Wave traffic may also pause during flashing.' : '')+' Keep power connected.','Flash Zigbee radio');break;
      case 'radio-probe':openModal('radio:probe',isCC2652() ? 'Detect CC2652 variant?' : 'Probe Zigbee radio info?',isCC2652() ? 'Query the CC2652 ROM bootloader to identify the radio variant. Radio communication is briefly interrupted.' : 'This temporarily pauses the serial server to query the Zigbee radio.',isCC2652() ? 'Detect variant' : 'Probe radio');break;
      case 'radio-bootloader':openModal('radio:bootloader','Trigger bootloader manually?','Prepare the Zigbee radio for flashing with an external tool. Reset the radio afterward to resume normal operation.','Trigger bootloader');break;
      case 'radio-reset':openModal('radio:reset','Reset Zigbee radio?','This interrupts the Zigbee serial connection. The ESPHome gateway stays running.','Reset radio');break;
      case 'serial-pause':zigbeeAction('pause');break;
      case 'serial-resume':zigbeeAction('resume');break;
      case 'radio-baud':zigbeeAction('baud',q('#tzb-radio-baud').value);break;
      case 'radio-url-clear':q('#tzb-radio-url').value = '';zigbeeAction('url','');break;
      case 'radio-url-save':{
        const value = q('#tzb-radio-url').value.trim();
        if (value && !isGBLURL(value)) { notify('Enter a direct HTTP(S) .gbl file URL, or leave it empty.',true);break; }
        zigbeeAction('url',value);break;
      }
      case 'check-update':deviceAction('check-update');break;
      case 'install-update':openModal('install-update','Install device firmware?','Install '+device.latest_version+' for '+device.model+'? The gateway will restart after installation.','Install firmware');break;
      case 'choose-file':q('#tzb-firmware-file').click();break;
      case 'review-file':{
        const file = q('#tzb-firmware-file').files[0];
        if (file && file.name.endsWith('.ota.bin')) openModal('upload','Install device firmware?',file.name+' — confirm this OTA image is for '+device.model+'.','Upload firmware');
        else notify('Choose this device’s .ota.bin image. Factory images must be flashed over USB.',true);
        break;
      }
      case 'cancel-modal':closeModal();break;
      case 'confirm-modal':{ const action = modalAction;closeModal();if (action === 'internet:on') setInternetUse(true);else if (action === 'upload') uploadFirmware();else if (action.startsWith('radio:')) zigbeeAction(action.slice(6));else deviceAction(action);break; }
      case 'logs':if (logs) stopLogs();else startLogs();break;
      case 'download-logs':downloadLogs();break;
      case 'clear-logs':logRecords = [];logBytes = 0;droppedLines = 0;captureStarted = logs ? new Date().toISOString() : null;renderLogs();break;
      case 'copy-details':if (device && network) supportDetails();break;
    }
  });
  root.addEventListener('keydown',event=>{
    if (!q('[data-ui="modal"]').hidden) {
      if (event.key === 'Escape') { closeModal();return; }
      if (event.key === 'Tab') {
        const first = q('[data-action="cancel-modal"]'), last = q('[data-action="confirm-modal"]');
        if (event.shiftKey && document.activeElement === first) { event.preventDefault();last.focus(); }
        else if (!event.shiftKey && document.activeElement === last) { event.preventDefault();first.focus(); }
      }
    }
  });
  q('[data-ui="log-panel"]').addEventListener('toggle',()=>{
    if (q('[data-ui="log-panel"]').open && !q('[data-ui="log-panel"]').hidden) startLogs();
    renderLogs();
    if (q('[data-ui="log-panel"]').open) q('[data-ui="logs"]').scrollTop = q('[data-ui="logs"]').scrollHeight;
  });
  q('[data-ui="network-form"]').addEventListener('submit',event=>{event.preventDefault();applyNetwork(formValues());});
  q('[data-ui="network-form"]').addEventListener('keydown',event=>{if (event.key === 'Enter') {event.preventDefault();if (!network?.testing && !busy) applyNetwork(formValues());}});
  q('[data-ui="offline-switch"]').addEventListener('change',()=>{
    const offline = q('[data-ui="offline-switch"]').checked;
    q('[data-ui="offline-switch"]').checked = Boolean(network?.use_without_internet);
    if (offline) openModal('internet:on','Use this device without internet?',
      'Using the device without internet access prevents it from checking for new firmware. Be sure to look for new releases to stay up to date.',
      'Use without internet');
    else setInternetUse(false);
  });
  q('[name="mode"]').addEventListener('change',modeChanged);
  qa('[data-channel]').forEach(input => input.addEventListener('change',()=>zigbeeAction(input.dataset.channel === 'gateway' ? 'gateway-channel' : 'zigbee-channel',input.checked ? 'beta' : 'stable')));
  for (const field of fields) q('[name="'+field+'"]').addEventListener('input',validateForm);
  q('#tzb-firmware-file').addEventListener('change',event=>{
    const file = event.target.files[0];q('[data-ui="file-name"]').textContent = file ? file.name : 'No file selected · .ota.bin';q('[data-ui="review-file"]').hidden = !file;
    q('[data-action="review-file"]').classList.toggle('tzb-primary',Boolean(file && file.name.endsWith('.ota.bin')));
  });
  document.addEventListener('visibilitychange',()=>{if (document.hidden) clearTimeout(pollTimer);else schedulePoll(0);});
  window.addEventListener('pagehide',()=>{clearTimeout(pollTimer);stopLogs();});
  window.addEventListener('pageshow',event=>{if (event.persisted) schedulePoll(0);});
  setInterval(()=>{
    if (!network?.testing || restarting) return;
    const remaining = Math.max(0,Math.ceil((trialDeadline-performance.now())/1000));
    bind('countdown',String(remaining));
    if (!remaining) showReconnect(network.return_ip || network.hostname,false);
  },1000);
  appearance();renderLogs();render();refresh();
})();
