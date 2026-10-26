# MSA-Kit 0.1.0 validation

The implemented server working tree was built and tested on Linux. This report
records the completed checks for the input-test expansion.

## Environment

| Item | Observed value |
| --- | --- |
| Platform | Linux 6.8.0-136-generic, x86_64, glibc 2.35 |
| Compiler | GCC 13.1.0 |
| CMake | 3.26.4 |
| System zlib | 1.2.11 |
| Test interpreter | Python 3.10.12 |
| Bundled headers | CLI11 2.6.1; spdlog 1.16.0; doctest 2.4.12 |
| Program version | `msakit 0.1.0` |

The HAlign-5 source snapshot and dependency file hashes are recorded in
[the dependency manifest](../extern/provenance.json). All **137** listed header
and license files were checked against their recorded SHA-256 values.

## Completed builds and tests

| CTest entry | Cases | Release | Debug + ASan/UBSan |
| --- | ---: | --- | --- |
| `unit.contract` | 21 | Passed | Passed |
| `unit.reference` | 8 | Passed | Passed |
| `integration.contract` | 54 | Passed | Passed |
| `integration.reference` | 2 | Passed | Passed |
| Total | **85** | **4/4 CTest entries passed** | **4/4 CTest entries passed** |

The 29 doctest cases performed **7,317 assertions** per build. The 56 Python
integration methods include additional parameterized subcases. The two doctest
groups filter each other's cases; those filtered cases are executed in the
other group, not omitted from the full run.

The final CTest runs reported zero failures. Their elapsed suite times were
about 2.24 seconds for Release and 15.74 seconds for the instrumented build.
These are test-suite durations, not alignment performance measurements.

The pre-expansion Release baseline passed all four entries before editing.
All 68 original test cases/methods were retained. The expansion adds seven
doctest cases and ten Python methods, including a catalog-integrity check that
does not start a subprocess. Its new input matrices execute **603 subprocess
scenarios** per full run; these are parameterized combinations, not separate
CTest entries. The added module cases contain 326 byte/context/boundary/read
combinations, including all 256 byte values.

A separate `ctest -L contract` run passed **2/2** entries. The complete Python
contract suite was also run with the existing valid test core, which aligns by
right-padding to the longest sequence instead of using star alignment. All
**54 methods passed** with that core. The test-only wrapper is retained in the
ignored evidence directory; the production executable and CLI are unchanged.

No Python methods were skipped in these runs. The doctest filtered-case counts
refer only to the complementary contract/reference group described above.

During the original implementation, the executable was also built with
`BUILD_TESTING=OFF`, installed under an
isolated prefix inside its build directory, and executed with `--version`.
No Python package lookup was required for that build. The install target and
standalone executable completed successfully without changing system software.

During that initial validation, the checked-in [tiny input](../examples/tiny.fasta) was aligned with the Release
executable, and its named output matched
[the expected FASTA](../examples/tiny.expected.fasta) byte for byte.

## Coverage

- The [input testing matrix](testing.md) maps stable identifiers to expected
  records, counters, error locations, and file outcomes. Fixed gzip variant
  identifiers are explicit and checked for uniqueness.
- The full byte domain, contextual LF/CRLF/CR and header markers, first/middle/
  last malformed records, and exact source/line/record diagnostics.
- Twelve representative valid inputs across all four input and three output
  transports; 17 invalid FASTA cases through all four input transports.
  Seed 20261026 supplies 32 small generated record sets with four transports.
- Ordinary and gzip files, non-seekable stdin, clean stdout, complete headers,
  duplicate names and sequences, record order, LF/CRLF, and final-line behavior.
- Exact byte-cleaning rules and disjoint counts, retained U, pre-existing gap
  rejection, empty or malformed input, and all documented error categories.
- gzip magic detection, concatenated and empty members, truncated streams,
  CRC corruption, trailing garbage, and both input/output buffer boundaries.
  The largest stream fixture is one deterministic 250,000-character sequence,
  so it does not invoke a large pairwise dynamic-programming calculation.
- Optional gzip headers and header CRC, damaged method/flags/payload/CRC/size,
  all incomplete prefixes of a tiny member, and member splits inside headers,
  CRLF, and sequence text. Valid fixtures are independently checked by Python
  gzip and per-member zlib decoding.
- Forty precise buffer-boundary combinations, including the plain-input
  two-byte detection prefix; deterministic short reads, EINTR recovery, and
  input read/close errors with exactly-once descriptor-release checks.
- Default/custom/unwrapped output, overwrite refusal, complete forced
  replacement, same-file and link aliases, and preservation of existing files
  on failures.
- Deterministic injection of write, gzip-finalization, close, publication,
  and allocation errors; short/interrupted writes; real `/dev/full` and
  closed-pipe failures; publication races and temporary-name reuse.
- Rejection of faulty core results and exceptions, plus acceptance of a
  different valid layout from a separate test core.
- The reference star example, scoring and tie rules, insertion-slot merging,
  independent exhaustive short-pair optimal-score checks, seeded small
  multiple-alignment cases, input preservation, and arithmetic bounds.

The application has no public fault-injection flags. The fake cores and file
operation substitutes are confined to separate test targets and internal test
interfaces.

No input semantics or alignment algorithm required repair during this expansion.
Production changes add only the internal input-operation boundary with real
system-call defaults. Test improvements also remove an over-broad CR assertion
that would reject otherwise valid complete headers, make fixture IDs stable,
and include the command and captured streams in matrix-failure diagnostics.

## Reproducing the checks

Run from the repository root on a Linux system with the prerequisites described
in [README](../README.md):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
(cd build && ctest --output-on-failure)

cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DMSAKIT_ENABLE_SANITIZERS=ON
cmake --build build-sanitize --parallel 2
(cd build-sanitize && ctest --output-on-failure)
```

To check only the algorithm-independent contract after replacing the core:

```bash
(cd build && ctest -L contract --output-on-failure)
```

For the final recorded runs, CTest also received
`--output-junit input-test-results.xml`. This reporting option is available in the
observed CMake version; it is not required for ordinary builds or tests. The
server retains the generated JUnit reports at
`build/input-test-results.xml` and `build-sanitize/input-test-results.xml`.
The contract-only run has `build/input-contract-results.xml`. Full logs are
preserved in `build-input-tests-evidence/release-full.log` and
`build-input-tests-evidence/sanitize-full.log`, since subsequent filtered CTest
runs replace a build's `Testing/Temporary/LastTest.log`.

The same ignored evidence directory contains the build logs, final CTest
summaries, `contract-only.log`, `alternate-core-contract.log`, the original
baseline log, and `baseline.tar.gz` with the pre-expansion source/tests/docs.

The original implementation's separate runtime-only check used:

```bash
cmake -S . -B build-runtime-only -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE
cmake --build build-runtime-only --parallel 2
cmake --install build-runtime-only --prefix "$PWD/build-runtime-only/install"
./build-runtime-only/install/bin/msakit --version
```

CMake reported the Python-disable variable unused because this configuration
does not invoke Python discovery at all. Users need only `BUILD_TESTING=OFF`.

## Sanitizer configuration

The first instrumented run exposed intermittent process-startup crashes, also
affecting `--version`. A separate empty program compiled with the same compiler
and sanitizers reproduced them: the default PIE build failed in 12 of 30
launches, while the non-PIE control completed all 30 launches.

The sanitizer configuration therefore uses `-fno-pie` and `-no-pie` only for
instrumented builds. The Release executable retains its ordinary PIE layout.
The instrumented executable was checked to link both `libasan` and `libubsan`.
CTest enables leak detection and immediate failure for ASan/UBSan diagnostics;
no tests or sanitizer checks were disabled, and no suppression list was added.
The final full run completed without sanitizer error reports.

The original failed-run log remains in the ignored build directory as
`build-sanitize/asan-initial-failure.log`. The minimal control executables are
also retained there for investigation. No host ASLR or kernel setting was
changed.

## Interpretation and limits

This validates the documented software contract and the small reference
implementation. It does not measure biological alignment accuracy, large-data
scalability, or performance against HAlign-5 or another aligner. Protein/profile
input, parallel execution, and platforms other than Linux remain outside the
first version. CMake 3.18 is the declared minimum; the observed acceptance
environment used CMake 3.26.4.
