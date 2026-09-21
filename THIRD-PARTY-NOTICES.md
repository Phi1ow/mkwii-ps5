# Third-party notices

The included project license text is GPL-3.0; see [LICENSE](LICENSE). Existing per-file notices remain applicable.

Dependency source trees are fetched at the revisions recorded in `sources.lock.json`. Preserve their license and notice files when redistributing source or binaries:

- WiiCompiled: GPL-3.0, with its own `THIRD-PARTY-NOTICES.md` and notices for Aurora and runtime dependencies.
- SharpProspero: GPL-3.0 and its bundled third-party notices.
- ps5link-sdk: consult its pinned source license and shader notices.
- LLVM/libc++/libc++abi and the dependencies declared in `ps5/toolchain/`: retain their respective notices in binary distributions.

The game disc data, console runtime library and player saves are not covered by the project's GPL license. No ownership of Nintendo or Sony materials is claimed.

The final binary's corresponding-source provenance and complete binary license bundle are still pending verification. The local release candidate must not be described as ready for public redistribution until that work is complete.
