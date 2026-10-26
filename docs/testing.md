# Input handling tests

MSA-Kit's input tests describe what each input means, how it is cleaned or
rejected, and what may happen to output files. They use synthetic bytes and
small sequences. The authoritative behavior remains the
[design specification](design.md); actual runs are recorded in
[the validation report](validation.md).

## Test organization

- [The input-case catalog](../tests/input_cases.py) contains named inputs and
  expected records, cleaning counts, or error locations. Expected values do not
  come from running the production parser.
- [The multi-sequence catalog](../tests/msa_cases.py) adds six checked-in FASTA
  files and seven bounded generated datasets, all with at least two records.
- [The integration runner](../tests/integration.py) launches the real program,
  feeds files or pipes, independently parses output, and checks file lifetimes.
- [The I/O module tests](../tests/test_io.cpp) cover all byte values, exact
  buffer boundaries, and deterministic failures at the internal I/O boundary.
- [The core tests](../tests/test_core.cpp) retain independent short-pair scoring
  checks and result validation. Exact star-alignment layouts remain in the
  `reference` group.

The four CTest entries remain `unit.contract`, `unit.reference`,
`integration.contract`, and `integration.reference`. A case identifier or a
parameterized subcase is not a separate CTest entry. Reports distinguish
doctest cases, Python test methods, and parameter combinations.

## Input matrix

The stable identifiers below appear in assertions or Python subtest contexts.
The catalog contains the exact raw bytes and expected values for each case.

| Behavior | Case family | Expected handling | Layer |
| --- | --- | --- | --- |
| Uppercase, lowercase, N, and mixed T/U | `INPUT-VALID-*`, `INPUT-BYTES-*` | Preserve uppercase and U; uppercase the six accepted lowercase letters; report disjoint counts | Module and integration |
| IUPAC ambiguity, digits, whitespace, NUL, high-bit bytes | `INPUT-VALID-*`, `INPUT-BYTES-*` | Other sequence bytes become N, including every byte of non-ASCII sequence text | Module and integration |
| Existing dash or dot gaps | `INPUT-ERROR-*`, `INPUT-BYTES-*` | Content error, exit 3; never clean gaps into N | Module and integration |
| Complete headers, duplicate names/records, embedded header characters | `INPUT-VALID-004/005/009`, `HEADER-LONG` | Preserve full header bytes and positional identity; never wrap headers | Integration |
| Multiline sequences, LF/CRLF, empty lines, final line without LF | `INPUT-VALID-007/008`, `INPUT-STRUCTURE-001`, `INPUT-BOUNDARY-001` | Parse the same records; remove only structural line endings | Module and integration |
| Empty or malformed records in first, middle, or last positions | `INPUT-ERROR-*` | Exit 3 with source, record, and available physical line | Integration |
| BOM, content before a header, header indentation, FASTQ | `INPUT-ERROR-*` | Exit 3 under the existing FASTA contract | Integration |
| Ordinary/gzip contents independent of filename | `INPUT-VALID-*`, `INPUT-ERROR-*` | Content-based detection; four equivalent input transports | Integration |
| Optional gzip headers, header CRC, empty and concatenated members | `GZIP-VALID-*` | Decode one continuous FASTA stream | Integration |
| Damaged gzip header, payload, CRC, size, later member or trailer | `GZIP-ERROR-*`, `GZIP-PREFIX-*` | Exit 4 once gzip magic is recognized | Integration |
| Empty input and one-byte gzip prefix | `GZIP-PREFIX-*` | Ordinary input detection; exit 3 | Integration |
| CRLF, header and sequence boundaries near 64 KiB | `INPUT-BOUNDARY-*` | Block boundaries do not alter records or counts | Module |
| Single-byte/short reads, EINTR, read and close failures | `INPUT-READ-*` | Retry recoverable interruptions; report read/close failure as exit 4 | Module |
| Case, wrapping, line endings and gzip splits of small generated records | `GENERATED-*` | Preserve expected cleaned records and alignment invariants | Integration |
| Invalid input with stdout, new output, or forced replacement | Input and gzip error families | Empty stdout; no new result; preserve old target and source; no temporary files | Integration |
| Directory supplied as input | `PATH-INPUT-DIRECTORY` | Exit 4; leave directory contents and existing output intact | Integration |

The existing CLI, output-write, publication-race, core-failure, and reference
algorithm tests continue to run alongside this matrix.

The fixed catalog currently contains 12 valid FASTA cases, 17 content-error
cases, 8 valid gzip variants, 13 gzip-error variants, and 26 incomplete prefixes
of a small gzip member. Together with the 32 generated groups and path/header
checks, the expansion adds 603 subprocess scenarios. These are parameterized
scenarios inside test methods, not 603 new CTest entries. A separate catalog
integrity test checks identifier uniqueness and lifecycle-case references.

## Multi-sequence result checks

[The fixed FASTA files](../tests/data/msa/README.md) each contain six synthetic
sequences. Their prepared records are explicitly declared in the multi-sequence
catalog, independently of the program's parser and returned alignment.

| Stable case | Dataset or behavior |
| --- | --- |
| `MSA-FIXED-001` | DNA prefix/internal/suffix insertions, deletion and substitution |
| `MSA-FIXED-002` | RNA and mixed T/U, insertion, deletion and N |
| `MSA-FIXED-003` | Duplicate full headers and sequences, casing and unknown-character cleaning |
| `MSA-FIXED-004` | Divergent sequences and an N-rich center |
| `MSA-FIXED-005` | Input lengths 129, 1, 2, 9, 31 and 64 |
| `MSA-FIXED-006` | Input lengths 79, 80, 81, 159, 160 and 161 |
| `MSA-GENERATED-001` through `006` | Respectively 2, 3, 8, 16, 64 and 128 records; lengths 16–128 |
| `MSA-GENERATED-007` | Four related sequences of lengths 512, 513, 510 and 512 |

The generated MSA datasets use seed **20261027**, separate from the smaller
input-representation generator. All fixtures are synthetic; they are not
biological benchmarks or reference alignments.

Four new end-to-end methods add **127 subprocess scenarios**: 72 fixed-file
transport combinations, 28 generated-data combinations, 12 reversed/rotated
input orders, and 15 output-width combinations. The width checks cover 0, 1, 7,
80 and 81 through stdout and both named-output formats. Positive widths limit
each sequence line; width zero requires one unwrapped line per sequence.

Every result is parsed independently and must preserve complete headers, input
positions, record count and cleaned residues, with equal nonzero row widths,
only `ACGTUN-`, and no all-gap column. Reordering inputs is checked against the
new order; the resulting gap layout may change with the selected center.

Two further methods test this independent checker itself: it must accept two
distinct valid layouts and reject 16 malformed outputs plus two width errors.
These include unaligned unequal-length rows, missing/extra/reordered rows,
changed headers, altered/lost/added residues, illegal characters, all-gap
columns and malformed serialization. They do not call the production validator.

With the supplied reference core, every MSA dataset invokes pairwise alignment.
The largest pairwise score matrix in this set is 513 by 514 cells (2,109,456
bytes of scores); each table is released before the next query. These bounded
checks exercise actual alignment without a large-data benchmark. The existing
250,000-character single-sequence stream test remains a separate I/O check.

The [validation record](validation.md) lists observed output widths and the
location of retained input/output files for each dataset. Output widths are
observations of the reference core, not golden layouts imposed on other cores.

## Transport and output checks

Every representative valid catalog case is exercised through four input paths:
plain file, gzip file, plain stdin, and gzip stdin. Each path is combined with
plain stdout, a plain output file, and a gzip output file. The filenames include
spaces and non-ASCII text; some suffixes deliberately disagree with contents.

Success checks inspect the full headers, record order and count, cleaned
sequences after removing gaps, equal row lengths, permitted residue characters,
absence of all-gap columns, and the two cleaning counts. Named outputs must not
leak alignment data to stdout. Python independently decompresses gzip results.

The tests do not require a replacement core to return the same gap layout on
different runs. Only a single-row result, a specified test core, or a
`reference` test has an exact-layout expectation. Compressed bytes are not
compared, because gzip metadata and compression choices are not part of the
public alignment contract.

Header bytes are checked separately from sequence lines. In particular, an
embedded CR in a header is preserved when it was not part of a CRLF ending;
the output checker must not reject every CR byte globally.

Content errors are checked through all four input paths. Selected failures also
exercise both plain and gzip named outputs, including existing targets passed
with `--force`. A damaged gzip fixture starts from valid FASTA, so the test
isolates the container error rather than relying on precedence between
different simultaneous faults.

## Boundaries and generated inputs

The byte tests insert each value into a controlled FASTA context and check an
independent expectation. LF is tested as structure; CR inside a sequence is a
byte to clean unless immediately followed by LF. A `>` inside a sequence line
is distinct from a `>` at the start of a line. These structural cases are tested
separately rather than inferred from a character table.

Boundary fixtures position delimiters around the 64 KiB buffer size, including
the two-byte prefix consumed during input detection. Large fixtures contain
only one sequence, bypassing pairwise dynamic programming. The pre-existing
250,000-character roundtrip also tests compressed data that exceeds a buffer.

Input-operation substitutes deterministically limit reads and inject EINTR or
I/O errors. They do not depend on sleeps or operating-system pipe scheduling.
Close-failure substitutes release the real descriptor once before reporting an
error, matching the Linux ownership assumption used by the implementation.
These substitutes are internal; production CLI and environment variables do
not expose fault-injection controls.

Generated tests use seed **20261026** and **32 groups**, each with 1–6 records
and 1–32 residues per record. Expected records are chosen first; casing,
wrapping, LF/CRLF, and gzip-member splits then vary their representation. The
fixed cases remain the specification for edge conditions; generated cases add
combinations. Failures include the seed and case identifier.

gzip fixtures with optional headers are assembled independently in the test
catalog and checked with zlib, including the header CRC. Tiny gzip prefixes
cover every truncation point: fewer than two magic bytes follow ordinary input
detection, while recognized incomplete gzip streams fail as I/O errors.

## Running and extending the tests

From the repository root on Linux:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
(cd build && ctest --output-on-failure)

# Reusable contracts after changing an alignment core:
(cd build && ctest -L contract --output-on-failure)

# Verbose subprocess tests, including case context on failure:
python3 tests/integration.py --binary build/msakit \
  --test-driver build/msakit_test_driver --suite contract

# Focus on the new I/O module case families:
./build/msakit_unit_tests --test-case='*INPUT-*'
```

For memory and undefined-behavior diagnostics:

```bash
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DMSAKIT_ENABLE_SANITIZERS=ON
cmake --build build-sanitize --parallel 2
(cd build-sanitize && ctest --output-on-failure)
```

To add an input rule or regression:

1. Choose an unused stable identifier in the appropriate catalog family. Keep
   existing identifiers when adding or reordering cases.
2. Supply literal input bytes and independently determined expected headers,
   cleaned sequences and counts, or the documented error code and location.
3. Include only one fault when asserting its error category. For content errors,
   check the physical line where the problem is discovered and the affected
   record; an empty file has no valid physical line to assert. Match diagnostic
   meaning and location, not entire operating-system or zlib error strings.
4. Use the shared matrix for ordinary inputs. Put precise byte/read boundaries
   in module tests and container-specific cases in the gzip families. Select
   representative file-lifetime checks rather than multiplying every dimension.
5. Run the relevant tests, then the complete Release and instrumented suites
   after fixes. Update this matrix when introducing a new behavior family and
   record actual results in the validation report.

For example, a new successful catalog entry can state its expected values
directly:

```python
InputCase("INPUT-VALID-013", b">example\nacR\n",
          (b"example",), (b"ACN",), uppercase=2, unknown=1)
```

This is an illustration of adding a case, not an additional checked-in fixture.
The input is automatically exercised through the valid-case transport matrix
when the entry is added to `VALID_CASES`.

No real sequence data, downloads, optional testing packages, or changes to
system settings are needed. The tests establish software behavior, not
biological alignment accuracy or large-data performance.
