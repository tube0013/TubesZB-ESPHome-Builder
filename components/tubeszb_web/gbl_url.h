#pragma once
#include <string>

namespace esphome::tubeszb_web {

// Match a direct HTTP(S) GBL file, allowing signed queries and fragments.
inline bool is_gbl_url(const std::string &url) {
  if (url.size() > 255 || url.find_first_of("\r\n\t ") != std::string::npos)
    return false;
  size_t host_start;
  if (url.rfind("https://", 0) == 0) host_start = 8;
  else if (url.rfind("http://", 0) == 0) host_start = 7;
  else return false;
  const auto path_start = url.find_first_of("/?#", host_start);
  if (path_start == std::string::npos || path_start == host_start || url[path_start] != '/')
    return false;
  const auto suffix_end = url.find_first_of("?#", path_start);
  const auto end = suffix_end == std::string::npos ? url.size() : suffix_end;
  return end >= path_start + 5 && url[end - 4] == '.' &&
         (url[end - 3] == 'g' || url[end - 3] == 'G') &&
         (url[end - 2] == 'b' || url[end - 2] == 'B') &&
         (url[end - 1] == 'l' || url[end - 1] == 'L');
}

}  // namespace esphome::tubeszb_web
