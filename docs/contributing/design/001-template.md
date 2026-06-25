# 001 - Design Doc Template

> Copy this file when starting a new design proposal. Number the file
> sequentially: `002-short-name.md`, `003-short-name.md`, etc.

## Problem

A paragraph describing what's wrong, missing, or insufficient about
the current state. Be concrete. "It would be nice if..." is not a
problem statement; "compilation fails when programs define more than
256 globals because OP_GET_GLOBAL takes a u8 index" is.

## Proposal

A paragraph (or a few) describing what you want to do. The proposal
should be specific enough that a reader can skim it and understand
the change without reading the rest of the document.

## Detailed design

The actual mechanics. New types, new opcodes, new memory layouts,
sequence diagrams, whatever the change requires. If the change
touches `MEMORY_MAP.md` or `OPCODES.md`, sketch the edits to those
files here.

## Alternatives considered

What else could solve the same problem, and why this proposal is
better than each. There should always be at least one alternative,
even if it's "do nothing." If you can't think of an alternative,
either the problem is too narrow to need a design doc or you
haven't thought hard enough.

## Cost

- **Memory cost** (RAM and ROM, in bytes if you can estimate)
- **Performance cost** (cycles, dispatched opcodes per second)
- **Code complexity cost** (subjective; describe what gets harder)
- **Schedule cost** (rough estimate: hours? days? weeks?)

## Migration

If this changes existing behavior, what happens to existing tests,
existing programs, existing on-disk bytecode? Is there a deprecation
path or is it a clean break?

## Open questions

Things you don't know the answer to and want input on before
implementing. Phrase them as questions, not as TODOs.

## Decision

Record the final decision and any changes requested during review.
Rejected, deferred, or superseded proposals should not remain in this
folder unless they preserve implementation-critical context that has no
better home.
