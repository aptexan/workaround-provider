# nlohmann/json

Vendored single-header copy of [nlohmann/json](https://github.com/nlohmann/json) **3.11.3** (MIT, see `LICENSE.MIT`).
The provider uses it to read and write MessagePack (including the extension type that marks an unknown value) and to
read the JSON that OpenTofu stores a resource's state in. See `src/cty.cpp`.

- Source: `https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp`
- SHA-256: `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6`
- Checked 2026-11-06: byte-identical to `single_include/nlohmann/json.hpp` in the `v3.11.3` source tree, and the same size
  (919975 bytes) as the release asset. The release's GPG signature (`json.hpp.asc`) was NOT verified.

To update it: download the new `json.hpp` and `LICENSE.MIT`, update the version and checksum here, and run
`tools/run-all.sh` (the unit tests cover the msgpack and JSON behaviour the provider relies on).
