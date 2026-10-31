# EasyMSA Benchmark entry

Copy this directory with MSA-Kit when creating a new algorithm repository.
Commit all source and dependencies required by the build. The platform builds
offline in its fixed C++/CMake/Python environment:

```bash
bash benchmark/build.sh
bash benchmark/run.sh /absolute/input.fasta /absolute/aligned.fasta 4 dna
python3 -m unittest discover -s benchmark -p 'test_*.py' -v
```

The four run arguments are input FASTA, output FASTA, thread allowance and
`dna`/`rna`. The output parent directory must exist and output must not exist.
The reference core is single threaded. An implementation may use fewer threads
than the allowance, and must not exceed it.

Adapter identity: `msakit-easymsa-adapter-1`. MSA-Kit normally reconstructs kseq
titles and changes ambiguity codes to N. This explicit entry assigns unique
temporary IDs and masks IUPAC ambiguity letters other than A/C/G/T/U/N to N.
After the program returns it verifies each aligned row against that masked input
before restoring original IUPAC characters and complete titles by residue index.
T/U are never interconverted. The platform then independently validates output
against the original sequences. Only this adapter masks ambiguous characters;
other submitted programs receive original platform FASTA records.

Record the adapter version and `IUPAC ambiguity -> N before alignment; restore
original symbols afterwards` as method preprocessing. Alignment timing includes
the adapter. The normal MSA-Kit CLI, input contract and core are unchanged.
