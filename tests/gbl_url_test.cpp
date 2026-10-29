#include "components/tubeszb_web/gbl_url.h"
#include <cassert>
#include <string>

int main() {
  using esphome::tubeszb_web::is_gbl_url;
  for (const auto *url : {"https://example.com/radio.gbl", "http://192.168.1.2/radio.GBL",
       "https://example.com/radio.gBl?download=1#image"})
    assert(is_gbl_url(url));
  for (const auto *url : {"", "https://example.com/manifest.json", "https://example.com/radio.bin",
       "https://example.com/radio.hex", "https://example.com/radio.gbl.exe",
       "https://example.com/download?file=radio.gbl", "https://example.com/radio.gbl/",
       "https://radio.gbl", "https:///radio.gbl", "ftp://example.com/radio.gbl",
       "https://example.com/radio.gbl\n"})
    assert(!is_gbl_url(url));
  assert(!is_gbl_url("https://example.com/" + std::string(250, 'a') + ".gbl"));
}
