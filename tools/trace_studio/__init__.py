"""trace_studio — OpenLords2's port↔retail trace workhorse.

Runs retail and the port off the SAME input segtrace, captures label-keyed
frames on both sides, diffs them, and (Phase 2) serves a scrub/compare UI.
Modeled on ../openrecet/tools/trace_studio/ with deliberate deviations
(one op-compiler, label-keyed pairing, no D1/D2 engine gates) — see
docs/plans/trace-studio.md.
"""
