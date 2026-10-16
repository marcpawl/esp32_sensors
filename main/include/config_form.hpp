// Form and Config conversion for the configuration UI (§5.1, §5.3).
//
// The HTTPS router is page-agnostic, so the logic that turns an
// application/x-www-form-urlencoded POST body into a candidate Config lives
// here, next to the pages that render it. Everything in this module is pure
// (no HTTP, no IDF), so it is covered by the host unit tests.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app_config.hpp"

namespace thermo::config_form {

// A parsed form field (already URL-decoded).
using Field = std::pair<std::string, std::string>;

// URL-decodes a single component. `plus_is_space` should be true for
// application/x-www-form-urlencoded data (+ means space).
std::string url_decode(const std::string& in, bool plus_is_space);

// Parses an application/x-www-form-urlencoded body into ordered key/value
// pairs. Pairs without '=' are skipped.
std::vector<Field> parse(const std::string& body);

// Returns the first value for `key`, or nullopt if absent.
std::optional<std::string> get(const std::vector<Field>& fields,
                               const std::string& key);

// Builds a candidate Config from a parsed form, starting from `current` so
// untouched fields survive. Per-sensor names arrive as `map_<ROMID>` fields and
// are merged into `mappings`. No validation is performed here.
Config to_config(const Config& current, const std::vector<Field>& fields);

}  // namespace thermo::config_form
