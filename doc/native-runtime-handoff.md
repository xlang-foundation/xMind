# Managed local runtime upgrades

This is a required delivery design, not an implemented or validated upgrade
protocol. The current launcher retains authenticated owners across folder changes
and host reloads, but matches them by runtime manifest and launch configuration.
When that match changes, it creates a new private database directory. Previous
records and owners remain stored and alive; the upgraded view does not thereby
inherit their sessions. That is a gap in managed upgrade continuity.

An upgrade must preserve the existing local profile's SQLite database, credentials,
sessions, transcript, events, provider configuration and completed run metadata.
SQLite access and migrations remain in C++ through embedded xlang3. Views never
open or copy the database. A package or folder change cannot terminate a ready
owner or replay its agents, tools, approval decisions or model calls.

## Required ownership sequence

1. Independently verify the new package's complete native gate, exact source
   revision, runtime inventory and thin view assets. Keep its runtime files in
   an immutable generation directory outside all opened workspace folders.
   Installing a new view must not replace DLLs loaded by another live generation.
2. Locate the authenticated owner for the same local profile and physical
   workspace. Compare its current authority with the retained owner record.
   Keep that owner's sessions available while an upgrade is pending; do not
   create an empty replacement database as an implicit migration.
3. Ask the native owner to quiesce using an authenticated owner command and its
   exact expected workspace/authority. This requires an atomic admission barrier.
   A read followed by a process kill is insufficient: another client could admit
   work between those actions.
4. Native rejects quiescence while queued/running/paused roots, owned children,
   context maintenance or other execution owners are active. It closes admission
   before publishing a durable receipt. Provider/configuration and incoming
   A2A/MCP admission paths must respect that same barrier. Quiescence never
   cancels work to manufacture an idle state.
5. An explicit retirement command consumes the exact native receipt and shuts
   down the old server cleanly. A lost response is an unknown outcome, not
   permission to retry a mutation or start a second owner. Observe the same
   generation's receipt/process and wait for its actual exit and lease release.
6. Start the verified new generation with the same owned profile database and
   verified private storage. Only the native database lease holder can apply
   migrations and reopen admission. Bind the new authenticated workspace
   authority before any client resumes mutations. Preserve encrypted credentials
   and use explicit authentication/session migration rather than inventing
   restored access.
7. Compare saved API records and accepted public metadata before admitting new
   work. Reconnect browser/VS Code/CLI views to the new generation and verify
   actual transcript, model selection, metrics and skill selections. Never
   overwrite its database with an older backup after it admits new work.

The generic local lifecycle contract belongs in xMind OSS. Nexus supplies its
private server/cluster lifecycle separately; this design adds no PostgreSQL,
team-server or Electron implementation to OSS.

## Protocol boundaries

Native needs owner status, quiescence inspection, quiescence admission and
retirement commands. Their final route names and records must be defined with
the implementation. Public generation identifiers and revision/authority fields
are preconditions, not access tokens. Owner lifecycle commands require native
owner authorization; they are never model tools and a guide or tool approval
cannot grant this authority. Browser view-session permissions must be defined
explicitly rather than inheriting retirement rights from general view routing.

The native barrier must cover in-flight admissions as well as new requests.
Persisted execution/maintenance ownership and the runtime's actual idle state
must agree. A view's empty run list or a stale `idle` flag is insufficient.
Do not move this barrier, migration logic or run ownership into JavaScript.

Legacy owners without this protocol cannot receive a fabricated retirement
receipt. Preserve access to their existing generation, report the missing
capability and require an explicit operator shutdown/migration path. Do not
silently force termination, fork their database while they can still mutate it,
or treat an unavailable owner as proof of safe migration.

## Required acceptance

- Race a normal submission and an incoming A2A/MCP admission against quiescence;
  prove that exactly the admitted owner or the retirement barrier wins.
- Reject quiescence for running/queued/paused graph and agent work, active children,
  maintenance, and unreconciled runtime faults; preserve all work and approvals.
- Retain the exact prior state after wrong authority, stale receipt, storage
  escape, package mismatch or a failed migration. Never replay effects.
- Lose the retirement response and observe the same generation without retrying
  the command or starting a competing database owner.
- Complete a real provider run, change package generation, preserve its exact
  records, restart against the same SQLite database, and complete another real
  run. Verify credentials, model context, user/model skill provenance and metrics.
- Keep already-open browser and VS Code views consistent; an older view cannot
  continue mutating a retired generation or overwrite the new owner's state.
- Verify Unicode and case-sensitive workspace/storage identities, a replaced
  physical root, multiple views, unavailable legacy owners and host reloads.

No upgrade continuity completion is claimed until these cases and actual
installed/rendered acceptance pass.
