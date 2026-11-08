# Conventions for commits and comments

The aim: a reviewer who has never seen this project can tell from a commit
or a comment what was changed, why, what it rests on, and how sure we are.

## Commit messages

Follow Haiku's style, with more detail in the body than usual.

```
component: what changed, imperative, at most 72 characters

Why: the problem or goal, with the symptom if there was one
(log line, test result, review finding).

What: the change, one paragraph or a short list per part. Name the
functions touched when that helps a reviewer find them.

Based on: the sources the change relies on, as file and function, e.g.
Linux drivers/usb/core/urb.c usb_submit_urb(), Haiku
src/add-ons/kernel/bus_managers/usb/Stack.cpp Stack::Explore(),
Bluetooth Core Specification Vol 4 Part E 7.7.14.

Verified: what was checked and how (check-module.sh, link-check.sh,
hardware run with date and device). Mark anything inferred but not
checked as [H] and say what would confirm it.

Not done / known limits: anything left out on purpose.

Co-Authored-By: ...
Claude-Session: ...
```

- Component prefixes: `linuxkpi`, `linuxkpi_wlan`, `linuxkpi_usb`,
  `linuxkpi_bt`, `mt7921e`, `manager`, `lkpictl`, `tools`, `docs`,
  `patches`.
- One logical change per commit where practical; a review-fix commit
  lists each finding and its fix.
- Keep the attribution trailers. They tell readers which code was written
  with AI assistance.

## Code comments

Comment where a reader would otherwise have to rediscover something:

- **Why, not what.** Explain intent, constraints and choices; don't
  narrate obvious code.
- **Source of behaviour.** When code reproduces Linux, FreeBSD or Haiku
  behaviour, name the original (file and function) so it can be checked.
- **Differences from Linux.** Every place where behaviour differs from
  Linux gets a comment there, plus a line in the subsystem's doc.
- **Assumptions about Haiku internals** are marked [V] (read in source,
  give the function) or [H] (inferred), as in the docs.
- **Locking and lifetime.** At each lock: what it protects, and whether it
  may be held across blocking calls. At each shared object: who frees it
  and what keeps it alive. For each buffer passed across an API: who owns
  it afterwards.
- **Context.** Note when a function runs in a completion or notification
  callback, may sleep, or must not take a given lock.
- **File header.** Copyright and licence, which side of a bridge the file
  is on (LinuxKPI or Haiku), and a short overview of the model.

## Imported code

Imported files are copied verbatim and never edited. Their provenance (exact
upstream commit, path, licence, checksum) goes in `IMPORTED.md` or a
`PROVENANCE.md` next to them. Adaptations live in LinuxKPI, not in the
imported files.

## Upstreaming to Haiku

Haiku's `AGENTS.md` says: "No AI-generated code. Every contribution must be
written by a human. Code, documentation, or any other materials generated
or modified with AI assistance are not accepted."

This repository is developed with AI assistance, so its code and patches
cannot be sent to Haiku as they are, however well documented. Detailed
commits and comments make the work easy to review and to learn from, and
they can serve as a specification. Any change proposed upstream (Gerrit)
must be written by a person and go through Haiku's normal review.
