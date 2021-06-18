folders
=======
- include
  - c++ headers
  - core: chainblocks.h
  - rest are utilities, meant to be consumed by external code (e.g. native modules)
  - *TODO: high level comment at the top of each file*
- rust/
  - library consumed
  - but also use chainblocks in other project (cf. lib.rs)
- src/
  - core/
    - engine
    - high performance
    - close to the metal
  - extra/
    - other stuffs not part of core
  - mal/
    - main function in stepA_mal.cpp
	- Core.cpp (MAL core)
	- CoreCB.cpp (chainblock specific)


history
=======
started with Joerg, AI company, used to work with nim language


implementing a new block
========================
- blocks_macros (factory, TO BE REPLACED)
- e.g. Uglify (edn.cpp)

inside: include shared
outside: include block_wrapper

uses composition and mixins

1. create a struct
2. `parameters()` (reflection of the block)
3. `setParam()`
  - CBVar is an union (can be a lot of types), 32 bytes size at the moment
4. `getParam()`
5. `inputType()`, `outputType()` (reflection)
6. `compose()`

node: CPU thread
chain: green thread

before a chain runs -> prepare, compose, then run

during compose: lifting, resolution of types

7. `warmup()` runs only once,
8. `cleanup()` might run multiple times, happens when the chain is stopped (reset the state, might keep some resources around)
9. `activate()` it's the "run" method, takes input and produce output


----
rust

blocks.rs: block trait

example: dummy block

registerRustBlock (it's a hack)

----

TODO:
- need to categorize blocks (low-level, core, high-level, other categories).
  - Most users will only use high-level blocks, only experts will use low-level ones (as it then become almost like programming)
- need a way to have a non-tech user-friendly documentation mostly based on visuals and another kind of doc very technical
  - for instance animated image/video showing example usage of the block from within the VR/3D interface
  - (ideally generating such animations could be automated)

- Use AI to generate the interactive documentation (animation track)