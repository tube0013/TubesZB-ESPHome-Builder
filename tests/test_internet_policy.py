from pathlib import Path
import subprocess
import yaml

ROOT = Path(__file__).resolve().parents[1]

def test_policy_restore_and_commit(tmp_path):
    exe = tmp_path / 'internet-policy'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT),
                    str(ROOT / 'tests/internet_policy_test.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


def test_every_manifest_gates_online_actions():
    def plain(node):
        if isinstance(node, yaml.ScalarNode): return node.value
        if isinstance(node, yaml.SequenceNode): return [plain(x) for x in node.value]
        return {plain(k): plain(v) for k, v in node.value}
    gated = 'id(device_network_settings).internet_allowed()'
    checked = []
    def walk(node, guarded=False):
        if isinstance(node, list):
            for x in node: walk(x, guarded)
        elif isinstance(node, dict):
            for key, value in node.items():
                if key == 'if':
                    cond = str(value.get('condition', ''))
                    walk(value.get('then', []), guarded or gated in cond)
                    walk(value.get('else', []), guarded)
                elif key in ('update.check', 'component.update') and value == 'esphome_fw_updater':
                    assert guarded, key
                elif key in ('cc2652_flasher.check_update', 'efr32_flasher.check_update',
                             'cc2652_flasher.update_firmware', 'efr32_flasher.update_firmware'):
                    assert guarded, key
                else: walk(value, guarded)
    for path in (ROOT / 'manifests').glob('*.yaml'):
        data = plain(yaml.compose(path.read_text()))
        if 'script' not in data: continue  # Personal wrappers include a hardware manifest.
        walk(data)
        for name in ('check_connectivity', 'run_update_checks'):
            script = next(x for x in data['script'] if x['id'] == name)
            assert '!'+gated in str(script['then'][0])
            assert 'script.stop' in str(script['then'][0])
        if 'cc2652_flasher' in data:
            assert data['cc2652_flasher']['check_on_boot'] == 'false'
            assert data['cc2652_flasher']['check_interval_days'] == '0'
        assert data['update'][0]['update_interval'] == 'never'
        checked.append(path.name)
    assert len(checked) >= 22
    # Shared CC flash script gates before pausing or queuing a manifest request.
    assert 'internet_allowed() && id(cc2652_flash_session).begin' in (ROOT/'packages/cc2652-flash.yaml').read_text()
    source = (ROOT/'components/network_settings/network_settings.cpp').read_text()
    assert 'nvs_get_u8(handle, "offline_v1"' in source
    assert 'nvs_commit(handle)' in source
    assert 'update_->set_request_parent(&guarded_http_)' in source
    assert 'setup_priority::ETHERNET + 1.0f' in (ROOT/'components/network_settings/network_settings.h').read_text()


def test_native_manifest_transport_cannot_bypass_policy(tmp_path):
    """Direct ESPHome UpdateEntity checks also hit the actual guarded transport."""
    include = tmp_path/'include'
    http = include/'esphome/components/http_request/http_request.h'
    http.parent.mkdir(parents=True)
    http.write_text('''#pragma once
#include <memory>
#include <string>
#include <vector>
namespace esphome::http_request {
struct Header {};
struct HttpContainer {};
class HttpRequestComponent {
 public:
  virtual ~HttpRequestComponent() = default;
  void set_timeout(unsigned v) { timeout_ = v; }
  unsigned get_timeout() const { return timeout_; }
  std::shared_ptr<HttpContainer> start(const char *u, const char *m, const std::string &b,
    const std::vector<Header> &h, const std::vector<std::string> &c) { return perform(u,m,b,h,c); }
 protected:
  virtual std::shared_ptr<HttpContainer> perform(const char*, const char*, const std::string&,
    const std::vector<Header>&, const std::vector<std::string>&) = 0;
  unsigned timeout_{4500};
};
}
''')
    log = include/'esphome/core/log.h'
    log.parent.mkdir(parents=True)
    log.write_text('#pragma once\n#define ESP_LOGI(...) ((void)0)\n')
    source = tmp_path/'transport.cpp'
    source.write_text('''#include "components/network_settings/guarded_http.h"
#include <cassert>
using namespace esphome;
struct Parent : http_request::HttpRequestComponent {
 int requests = 0;
 std::shared_ptr<http_request::HttpContainer> perform(const char *url, const char *method,
    const std::string&, const std::vector<http_request::Header>&, const std::vector<std::string>&) override {
  ++requests;
  assert(std::string(url) == "https://example.test/manifest.json");
  assert(std::string(method) == "GET");
  return std::make_shared<http_request::HttpContainer>();
 }
};
int main() {
 network_settings::InternetPolicy policy;
 Parent parent; network_settings::GuardedHttp transport; transport.bind(&policy, &parent);
 auto check = [&]() { return transport.start("https://example.test/manifest.json", "GET", "", {}, {}); };
 assert(!check()); assert(parent.requests == 0); // Not restored yet.
 policy.restore(true); assert(!check()); assert(parent.requests == 0); // Saved offline boot.
 policy.set(false, [](bool) { return true; }); assert(check()); assert(parent.requests == 1);
 policy.set(true, [](bool) { return true; }); assert(!check()); assert(parent.requests == 1);
}
''')
    exe = tmp_path/'transport'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(include),
                    '-I', str(ROOT), str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


def test_cc2652_startup_identity_is_local_and_unconditional():
    def plain(node):
        if isinstance(node, yaml.ScalarNode): return node.value
        if isinstance(node, yaml.SequenceNode): return [plain(x) for x in node.value]
        return {plain(k): plain(v) for k, v in node.value}
    package = plain(yaml.compose((ROOT/'packages/cc2652-flash.yaml').read_text()))
    assert package['esphome']['on_boot'] == [{'priority': '-100', 'then': [{'script.execute': 'probe_cc2652_on_boot'}]}]
    probe = next(x for x in package['script'] if x['id'] == 'probe_cc2652_on_boot')
    assert probe['mode'] == 'single'
    assert 'internet_allowed' not in str(probe)
    assert 'is_trial_active' not in str(probe)
    assert 'http' not in str(probe).lower()
    actions = probe['then']
    pause = next(i for i, x in enumerate(actions) if 'stream_server.pause' in x)
    detect = next(i for i, x in enumerate(actions) if 'cc2652_flasher.detect_variant' in x)
    restore = next(i for i, x in enumerate(actions) if 'if' in x)
    assert pause < detect < restore
    assert actions[restore]['if']['then'][0] == {'stream_server.resume': {'id': '${cc2652_stream_server}'}}
    assert 'cc2652_boot_probe_was_paused' in str(actions[restore]['if']['condition'])
    checked = []
    for path in (ROOT/'manifests').glob('*.yaml'):
        data = plain(yaml.compose(path.read_text()))
        if 'cc2652_flasher' not in data: continue
        assert 'cc2652_flash' in data['packages']
        updates = next(x for x in data['script'] if x['id'] == 'run_update_checks')
        assert {'script.wait': 'probe_cc2652_on_boot'} in updates['then']
        assert 'cc2652_flasher.detect_variant' not in str(updates)
        assert 'request_cc2652_probe' not in str(updates)
        checked.append(path.name)
    assert len(checked) == 9
    frontend = (ROOT/'components/tubeszb_web/cc2652_support.cpp').read_text()
    assert 'cc_boot_probe_script_->is_running()' in frontend
