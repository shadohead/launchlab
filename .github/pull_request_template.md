## What and why

<!-- What this changes and why. For a firmware behaviour change, say how you checked it on a device, if you did. -->

## Test proof

<!--
Changes to firmware/ or scripts/firmware_tests.py, scripts/host_cc_cache.py or scripts/test-firmware.sh need one.
Commit your branch, run

    python3 scripts/firmware_proof.py host

and paste the JSON block it prints (with its fences) below, replacing this comment. After every push, run it again and replace the block:
the "Firmware test proof" check fails while the proof is for another tree. See CONTRIBUTING.md.
Changes elsewhere (site/, docs/, hardware/) need none.
-->

## HIL run (optional, contributor-reported)

<!--
If you ran the on-device scenarios on your own M5StickS3 (python3 scripts/firmware_proof.py hil, see CONTRIBUTING.md),
the proof block above already holds the result. Nobody but you can check it, so reviewers read it as your report.
Say which scenarios you ran and anything the device did that the checks did not catch.
-->

## Checklist

- [ ] The proof block is for the pushed head (or this pull request changes nothing the firmware suite reads).
- [ ] New behaviour has a host test in `firmware/tests` that fails without the change; bug fixes have one that reproduces the bug.
- [ ] No device backups, serial logs, personal recordings, keys or tokens are in the diff.
