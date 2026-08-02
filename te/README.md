# COR24 terminal and uploader tools

This directory groups the host tools used to communicate with the COR24-TB
ROM load-and-go monitor and the verified second-stage loader.

- `te/` contains the original C terminal/uploader.
- `te2/` contains the checksummed, retrying C host and its small COR24 loader.
- `te-rs/` contains the Rust uploader with echo synchronization and pacing.

Generated host binaries, Rust `target/` trees, and COR24 object/listing files
are not source and should not be committed. `te2/te2.lgo` is intentionally
tracked: it is a small bootstrap required at runtime, unlike the large demo
and suite images loaded after it.

See `../docs/ft232rl-experiments.md` for the FT232RL test history and recovery
procedure.
