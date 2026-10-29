/* Shared by the embedded settings page and the validation tests. */
(() => {
  "use strict";
  function parseIPv4(text) {
    if (typeof text !== "string" || text.length > 15 ||
        !/^(0|[1-9]\d{0,2})(\.(0|[1-9]\d{0,2})){3}$/.test(text)) return null;
    const octets = text.split(".").map(Number);
    if (octets.some(n => n > 255)) return null;
    return octets.reduce((value, n) => ((value << 8) | n) >>> 0, 0);
  }
  function unicast(ip) {
    const first = ip >>> 24;
    return first !== 0 && first !== 127 && first < 224 && (ip >>> 16) !== 0xa9fe;
  }
  function validate(values) {
    const errors = {};
    if (!['dhcp', 'static'].includes(values.mode)) return {mode: "Choose DHCP or Static."};
    if (values.mode === 'dhcp') return errors;
    const parsed = {};
    for (const field of ['ip', 'subnet', 'gateway', 'dns1', 'dns2']) {
      if (field === 'dns2' && values[field] === '') { parsed[field] = 0; continue; }
      parsed[field] = parseIPv4(values[field]);
      if (parsed[field] === null) errors[field] = "Enter four decimal numbers from 0 to 255, without leading zeros.";
    }
    if (Object.keys(errors).length) return errors;
    const {ip, subnet, gateway, dns1, dns2} = parsed;
    const hosts = (~subnet) >>> 0;
    if (!unicast(ip)) errors.ip = "Use a unicast address; loopback and link-local addresses are not supported.";
    if (subnet === 0 || hosts < 3 || ((hosts & ((hosts + 1) >>> 0)) >>> 0) !== 0) {
      errors.subnet = "Use a contiguous subnet mask, from /1 through /30.";
    } else {
      const host = (ip & hosts) >>> 0;
      if (host === 0 || host === hosts) errors.ip = "This is the subnet's network or broadcast address.";
      const gatewayHost = (gateway & hosts) >>> 0;
      if (!unicast(gateway) || gateway === ip || (gateway & subnet) !== (ip & subnet) ||
          gatewayHost === 0 || gatewayHost === hosts) {
        errors.gateway = "Use a different host in the same subnet.";
      }
    }
    if (!unicast(dns1)) errors.dns1 = "Enter a usable DNS IPv4 address.";
    if (dns2 !== 0 && !unicast(dns2)) errors.dns2 = "Enter a usable DNS IPv4 address, or leave empty.";
    return errors;
  }
  globalThis.TZBNetworkValidation = {parseIPv4, validate};
})();
