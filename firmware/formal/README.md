# LaunchLab firmware formal verification

This gate checks the factory-reset contract, without touching hardware or changing firmware images.

- **TLA+/TLC** checks the bounded reset/interruption model for invariant, temporal-property and deadlock failures, with up to three power cuts and three storage faults. A separate deliberate persist-first violation must produce the named counterexample. Six reachability checks require the model to actually exercise valid intent, partial erase, completion, resumption, blocked invalid intent and ordinary boot after wipe.
- **Lean 4.30.0** checks 14 timing and cancellation theorems for the modeled UInt32 clock/gesture. Warnings fail the gate; the audited theorems may depend only on Lean’s standard logical axioms.
- **Production C++ on the host** compiles a contract bridge for timing constants and erase scope, and runs the actual confirmation, storage fault and sketch/loop/USB wipe suites with fakes. It is not a replacement for the broader host, target-build or hardware release gates.

The abstract model assumes truthful button samples, durable acknowledged storage operations and that real writers respect the checkpoint. A fresh hold between observations is not a physical measurement. The Lean clock claims require a true gap shorter than one full wrap between readings. The model and the C++ bridge are not proofs of refinement, the compiled image or the flash hardware.

## Run before pushing

With JDK and a C++17 compiler installed, set `LAUNCHLAB_TLC_JAR` to the [official TLC jar](https://github.com/tlaplus/tlaplus/releases/tag/v1.8.0) with SHA-256 `fdffef247f563ef6edbfd44958d2a82db79e7564e07cb673262d896353fa6713`. Set `LAUNCHLAB_LEAN` to the matching [Lean 4.30.0](https://github.com/leanprover/lean4/releases/tag/v4.30.0) executable if it is not already on PATH. Optionally set `LAUNCHLAB_JAVA` to an explicit Java path and `CXX=clang++` to match GitHub CI. Then:

```sh
python3 firmware/formal/verify.py
```

On Linux x86_64 `bash firmware/formal/toolchain.sh CACHE_DIR` downloads and verifies the same official TLC jar and the exact Lean Linux archive (its paths are shown in the workflow). The TLC release asset can move; the verifier rejects any bytes other than the checked hash.

## Enforcement

The private GitHub Actions context is **Firmware formal verification**; the public **LaunchLab firmware gate** also runs the broader public host suite and precedes Pages deployment. Both report a result on every PR, including non-firmware changes. Running is not the same as branch protection; making a GitHub check required needs a branch rule and a supported repository plan.

For a new frozen release, the private HIL release driver runs this same verifier before building or touching a device and records a non-reusable pass. The release gate record then requires that pass for commits containing this verifier. Archived release receipts are not rewritten or reinterpreted. This is a release gate, not a local pre-push hook, and it cannot prevent someone from bypassing the release path without a repository/platform control.
