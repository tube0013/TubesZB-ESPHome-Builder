const fs = require('node:fs');
const vm = require('node:vm');
vm.runInThisContext(fs.readFileSync('components/network_settings/network_validation.js', 'utf8'));
const input = JSON.parse(fs.readFileSync(0, 'utf8'));
const result = input.kind === 'parse'
  ? input.cases.map(value => TZBNetworkValidation.parseIPv4(value) !== null)
  : input.cases.map(value => Object.keys(TZBNetworkValidation.validate(value)).length === 0);
process.stdout.write(JSON.stringify(result));
