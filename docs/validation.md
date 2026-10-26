# MSA-Kit 0.1.0 validation

The implemented server working tree was built and tested on Linux. This report
records the completed checks for input handling and multi-sequence output validity.

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
| `integration.contract` | 60 | Passed | Passed |
| `integration.reference` | 2 | Passed | Passed |
| Total | **91** | **4/4 CTest entries passed** | **4/4 CTest entries passed** |

The 29 doctest cases performed **7,317 assertions** per build. The 62 Python
integration methods include additional parameterized subcases. The two doctest
groups filter each other's cases; those filtered cases are executed in the
other group, not omitted from the full run.

The final CTest runs reported zero failures. Their elapsed suite times were
about 2.76 seconds for Release and 21.32 seconds for the instrumented build.
These are test-suite durations, not alignment performance measurements.

The original input-test expansion retained all 68 initial test cases/methods
and added seven
doctest cases and ten Python methods, including a catalog-integrity check that
does not start a subprocess. Its new input matrices execute **603 subprocess
scenarios** per full run; these are parameterized combinations, not separate
CTest entries. The added module cases contain 326 byte/context/boundary/read
combinations, including all 256 byte values. That expansion's final suite
contained 85 cases/methods. The current multi-sequence extension retains them
all and adds six Python methods, including four end-to-end methods with 127
subprocess scenarios and two independent output-checker self-tests.

A separate `ctest -L contract` run passed **2/2** entries during the input-test
expansion. For the current extension, the complete Python
contract suite was also run with the existing valid test core, which aligns by
right-padding to the longest sequence instead of using star alignment. All
**60 methods passed** with that core. The test-only wrapper is retained in the
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

## Multi-sequence datasets and observed outputs

All 13 datasets below contain multiple sequences and passed the independent
output-contract checks. The six fixed FASTA files each contain six records;
seven generated datasets use seed 20261027. These are synthetic software-test
datasets, not biological reference alignments. The observed alignment widths
are not assertions for replacement cores.

| Case | Sequences | Cleaned input length range | Output columns | Status |
| --- | ---: | ---: | ---: | --- |
| `MSA-FIXED-001` | 6 | 11–15 | 19 | Passed |
| `MSA-FIXED-002` | 6 | 6–9 | 9 | Passed |
| `MSA-FIXED-003` | 6 | 3–7 | 8 | Passed |
| `MSA-FIXED-004` | 6 | 8–20 | 20 | Passed |
| `MSA-FIXED-005` | 6 | 1–129 | 129 | Passed |
| `MSA-FIXED-006` | 6 | 79–161 | 161 | Passed |
| `MSA-GENERATED-001` | 2 | 16–128 | 128 | Passed |
| `MSA-GENERATED-002` | 3 | 16–128 | 129 | Passed |
| `MSA-GENERATED-003` | 8 | 16–128 | 138 | Passed |
| `MSA-GENERATED-004` | 16 | 16–128 | 145 | Passed |
| `MSA-GENERATED-005` | 64 | 16–128 | 241 | Passed |
| `MSA-GENERATED-006` | 128 | 16–128 | 276 | Passed |
| `MSA-GENERATED-007` | 4 | 510–513 | 513 | Passed |

The matrix tests cover files, gzip, stdin, output wrapping, and reordered
inputs. Every result preserves complete headers and cleaned sequence content
by input position, has equal nonempty rows containing only `ACGTUN-`, and has
no all-gap columns. The independent Python checker accepts two legal layouts
and rejects 16 damaged outputs plus two serialization-width errors.

A separate retained-output run writes each input, aligned FASTA and stderr
under `build-msa-evidence/results/`. Its `msa-results.csv` and `msa-results.json`
summarize sequence counts, input lengths, observed output widths and gaps.
The largest score matrix in these datasets is 263,682 cells, approximately
2.1 MB for scores; this is not a statement about total process memory.

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

No input semantics or alignment algorithm required repair. The earlier input
expansion added the internal input-operation boundary with real system-call
defaults. The current multi-sequence extension changes only tests, fixtures,
and documentation. Test improvements also remove an over-broad CR assertion
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
`--output-junit msa-test-results.xml`. This reporting option is available in the
observed CMake version; it is not required for ordinary builds or tests. The
server retains the generated JUnit reports at
`build/msa-test-results.xml` and `build-sanitize/msa-test-results.xml`.
Full logs are preserved in `build-msa-evidence/release-full.log` and
`build-msa-evidence/sanitize-full.log`, since subsequent filtered CTest
runs replace a build's `Testing/Temporary/LastTest.log`.

The same ignored evidence directory contains the build logs,
`targeted-release.log`, and `alternate-core-contract.log`. The earlier
input-test expansion's logs, original baseline and source snapshot remain in
`build-input-tests-evidence/`.

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
