# VQF

Unmodified `vqf/cpp/vqf.hpp` and `vqf/cpp/vqf.cpp` from
https://github.com/dlaidig/vqf, commit
`86ba56bdd3158b9b05f9f9fe5596866ba326438c`.
MIT license in `LICENSE`; copyright and SPDX headers retained.

Paper: Laidig and Seel, Information Fusion 91 (2023), 187–204,
https://doi.org/10.1016/j.inffus.2022.10.014.

Both firmware and native tests compile with `VQF_SINGLE_PRECISION`.
Upstream keeps the Butterworth filters in double precision.
The LaunchLab wrapper resamples measured timestamps to 100 Hz and uses
6D orientation only. No magnetometer, position, or absolute heading is supplied.
