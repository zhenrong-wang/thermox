# Reference Code

This directory contains pinned upstream projects used for research and validation. Reference
projects are not linked into Thermox and are not part of its production module API.

## Property references

### `properties/co2`

- Upstream: `git@github.com:zhenrong-wang/co2.git`
- Revision: `0691bd538e50e823067c8a39980f2d8164feead9`
- License: MIT
- Intended use: reference equations and regression data for a future carbon-dioxide property
  backend.

Current promotion blockers:

- no library-oriented build contract or automated unit tests;
- broad unprefixed global C symbols;
- missing returns in non-void functions;
- ambiguous boolean/relative-error expressions;
- extensive strict-compiler warnings;
- no stable error/status and derivative API for the nonlinear solver.

### `properties/water-steam-if97`

- Upstream: `git@github.com:zhenrong-wang/water-steam-if97.git`
- Revision: `f04f911b77e783f5ab7ef038baf67c9b0188dfad`
- License: MIT
- Intended use: reference IF97 equations and regression data for a future water/steam property
  backend.

Current promotion blockers:

- no namespaced library-oriented build contract;
- broad unprefixed global C symbols;
- missing returns in non-void functions;
- expressions that apply `fabs` to a boolean result instead of a numeric error;
- extensive strict-compiler warnings;
- no stable phase, validity, error/status, cache, or derivative API.

## Promotion policy

Reference code may become a Thermox module only after:

1. authoritative regression vectors are documented and automated;
2. undefined behavior and strict-compiler findings are resolved upstream;
3. the implementation is wrapped behind the Thermox property interface;
4. public symbols are isolated from other property backends;
5. valid state ranges, phase behavior, units, failures, and derivatives are explicit;
6. Release and sanitizer validation passes as part of the main build.

Keep GPL or otherwise incompatible research projects outside the production source and dependency
graph.
