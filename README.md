# devgroup

Operations repo for a single-host, multi-agent AI coding workspace
where each agent runs as its own Unix user with filesystem-enforced
sandboxing. Holds provisioning scripts, coordinator tooling
(`dg-relay`, `dg-release`, …), design docs, and an indexed library
of saga briefs that capture in-flight work assigned to agents.

## Documentation

| | |
|---|---|
| [docs/](docs/) | Full documentation index |
| [Intro](docs/intro.md) | Start here if you're new |
| [Overview](docs/overview.md) | System architecture and shape |
| [Summary](docs/summary.md) | One-page status snapshot |

Deeper reading:

- [Purpose](docs/purpose.md) — why this repo exists
- [Usage](docs/usage.md) — installing and bringing it up
- [Branching & PR strategy](docs/branching-pr-strategy.md) — how work flows through the coordinator
- [Agent briefing](docs/agent-briefing.md) — what an agent reads on its first task
- [`.lgo` format](docs/lgo-format.md) — COR24 load-image format spec
- [Rust toolchain updates](docs/update-rust.md)
- [Saga briefs index](tools/briefs/README.md) — what's in flight, what shipped

## License

MIT — see [`LICENSE`](LICENSE).

Copyright (c) 2026 Michael A. Wright.
