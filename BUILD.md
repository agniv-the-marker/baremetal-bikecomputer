# building this thing

claude handled this file, rip it.

bare-metal program, links against the class `libpi` + `lib/libm` + a few prebuilt staff objects. you need:

1. `arm-none-eabi-gcc` on your PATH (i built with 10.3, targeting arm1176jzf-s)
2. the class support tree (`libpi/` + `lib/libm/`) — found automatically, see below
3. `pi-install` + a pi on usb-serial, but only to flash. building doesn't need it.

## how the makefile finds libpi

resolved in priority order (top of `Makefile`, which claude handled for me):

1. a vendored snapshot in `deps/` (from `make vendor`) — wins if present
2. the `CS240LX_2026_PATH` env var if you set it
3. the repo one directory up (`../`) — just works when final_project sits inside the class repo

## building

```sh
make                       # build + flash over serial (pi-install)
make RUN=0                 # build only, no flash
make PROGS=tests/5-gps.c   # build/flash a test program
make clean
```

flashing needs the pi power-cycled (the bootloader only listens at boot). standalone vs dev boot + sd card setup are in [README.md](README.md).

## standalone builds (`make vendor`)

to build with no class repo and no env var (fresh machine, self-contained snapshot), vendor the deps once on a machine that has the class repo:

```sh
make vendor       # copies libpi + lib/libm into deps/, make now uses deps/
make RUN=0
make vendor-clean # remove the snapshot
```

after that, final_project builds on its own (still need the arm toolchain installed).

## what's in-repo vs external

in-repo: the app + drivers + `tests/` + `docs/` + `tools/`. [PROVENANCE.md](PROVENANCE.md) has exactly what's original vs ported. external: the toolchain, `pi-install`, `libpi`, `lib/libm` (resolved or vendored as above).
