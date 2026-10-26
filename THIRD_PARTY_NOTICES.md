# Sources and third-party notices

MSA-Kit is distributed under the [MIT license](LICENSE). The following upstream
notices apply to copied or adapted material and are retained separately where
the upstream license differs.

## HAlign-5

Source: [pinglu-zhang/HAlign-5, commit
`b0d5608811d44e30bc323da54f0583ceaaf684d2`](https://github.com/pinglu-zhang/HAlign-5/tree/b0d5608811d44e30bc323da54f0583ceaaf684d2).

The nucleotide-cleaning table in `src/io.cpp` is adapted from
`include/utils.h` at that revision. Its notice is **Copyright (c) 2026 HAlign-5
contributors**. The complete MIT permission and warranty text is included in
[LICENSE](LICENSE), together with the upstream copyright attribution.

The reader/writer organization, CLI conventions, and separation of preparation
from alignment informed this template's design. The FASTA parser, gzip stream
handling, output transaction, and minimal reference star core implement
MSA-Kit's documented contract. The provenance table in
[the design specification](docs/design.md) describes the retained concepts and
intentional differences. HAlign-5's other algorithms, datasets, and external
tool workflow are not bundled.

## Bundled header-only dependencies

The header trees below are copied unchanged from the same pinned HAlign-5
snapshot. Their version macros identify the listed releases. Upstream license
files missing from that snapshot were obtained from the corresponding release
tags; their Git blob identities are recorded as well.

| Component | Version | License and retained notice |
| --- | --- | --- |
| [CLI11](https://github.com/CLIUtils/CLI11/tree/v2.6.1) | 2.6.1 | [BSD-3-Clause license](extern/CLI11/LICENSE); University of Cincinnati and contributors |
| [spdlog](https://github.com/gabime/spdlog/tree/v1.16.0) | 1.16.0 | [MIT license](extern/spdlog/LICENSE); Gabi Melman and contributors |
| fmt, bundled with spdlog | As included in the pinned snapshot | [Retained fmt license](extern/spdlog/include/spdlog/fmt/bundled/fmt.license.rst) |
| [doctest](https://github.com/doctest/doctest/tree/v2.4.12) | 2.4.12 | [MIT license](extern/doctest/LICENSE.txt); Viktor Kirilov |

[extern/provenance.json](extern/provenance.json) records the source revision,
component versions, license-source identities, and SHA-256 of every bundled
header and license file. Build configuration never fetches newer versions.
doctest is used only when tests are enabled.

## System dependency

zlib is linked as a required system library and is not vendored here. Its own
license remains applicable; see [zlib's license](https://zlib.net/zlib_license.html).
Python 3 is used only by the optional test suite. No third-party license is
replaced by MSA-Kit's license.
