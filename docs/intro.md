# Introduction

> **Welcome.** This page is the entry point for someone landing on
> the repo for the first time. It's intentionally short. Pick whichever
> link below fits why you're here, or read on for the quick orientation.

## What this repo is

A working setup for hosting multiple AI coding agents on one Linux
host, where each agent gets its own Unix user, its own write-protected
sandbox, and its own restricted view of a shared workspace. The
filesystem permission model (not just polite prompts) is what keeps
the agents in their lanes.

There's also a coordinator role — a human (or a more-trusted
"admin" agent) running as the system owner who relays the workers'
output to GitHub. Workers never get push access or credentials.

## Read next, by goal

- **I want to understand the design.** → [`overview.md`](overview.md)
- **I want a one-paragraph status snapshot.** → [`summary.md`](summary.md)
- **I want to install this on my own machine.** → [`usage.md`](usage.md)
- **I'm an AI agent that just got onboarded.** → [`agent-briefing.md`](agent-briefing.md)
- **I want to know how PRs flow through this system.** → [`branching-pr-strategy.md`](branching-pr-strategy.md)
- **I'm here for the technical motivation.** → [`purpose.md`](purpose.md)
- **I want format specs (e.g. `.lgo`).** → [`lgo-format.md`](lgo-format.md)
- **I want all the docs.** → [docs/](.)

## What this repo is *not*

- Not a runtime / framework you import into your agent code.
- Not a multi-tenant SaaS — it's a single-host setup for one admin
  running multiple agents.
- Not specific to any one agent vendor (Claude, GPT, etc.). The
  isolation mechanism is OS-level and provider-agnostic.

## Coordinator briefs and saga state

Day-to-day coordination notes live under [`tools/briefs/`](../tools/briefs/).
Each brief is a self-contained spec for a piece of work assigned to
one or more agents. The `README.md` in that directory is the index.
If you're trying to understand what's currently in flight or recently
shipped, start there.
