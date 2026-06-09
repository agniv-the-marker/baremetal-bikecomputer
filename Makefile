# bike computer, makefile handled by claude. assumes /final_project is the working directory.
#
#   make                          build + flash the product (bikecomputer.c)
#   make PROGS=tests/5-gps.c      build + flash a test program
#   make RUN=0                    build only (skip pi-install)
#   make vendor / vendor-clean    snapshot libpi+libm into deps/ for standalone builds

CLASS_REPO := $(abspath $(CURDIR)/..)
ifeq ($(origin CS240LX_2026_PATH),environment)
  CLASS_REPO := $(CS240LX_2026_PATH)
endif
ifneq ($(wildcard $(CURDIR)/deps/libpi/mk/Makefile.robust-v3),)
  CS240LX_2026_PATH := $(CURDIR)/deps
else
  CS240LX_2026_PATH := $(CLASS_REPO)
endif
export CS240LX_2026_PATH

PROGS := bikecomputer.c

# shared drivers, linked into every program (tests included).
COMMON_SRC := gps_nmea.c gpx_log.c route.c osm_map.c    # gps parse + gpx log + overlays
COMMON_SRC += sdfat/emmc.c sdfat/fat32_min.c            # sd card (+dma) + safe fat32
COMMON_SRC += st7735.c standard-ascii-font.c            # color tft ui
COMMON_SRC += dma_irq.c power.c                         # dma-done irq + wfi helpers
COMMON_SRC += ssd1306-display-driver.c                  # retired: USE_TFT=0 oled fallback
COMMON_SRC += bmp280.c                                  # retired: dead sensor batch (tests/4)

# prebuilt staff objects: sw-uart (gps rx), hw-spi (tft), pl011 (parked hw-uart).
STAFF_DIR  := $(CS240LX_2026_PATH)/libpi/staff-objs
STAFF_OBJS := $(STAFF_DIR)/staff-sw-uart.o \
              $(STAFF_DIR)/staff-hw-spi.o \
              $(STAFF_DIR)/pl011-uart.o

CFLAGS_EXTRA := -I./sdfat           # headers for the ported sd/fat32 code
RUN = 1                             # flash over serial after building
TTYUSB =                            # set if the pi shows up on a non-default tty
BOOTLOADER = pi-install

include $(CS240LX_2026_PATH)/libpi/mk/Makefile.robust-v3

# copies libpi + lib/libm into deps/ so this folder builds with no class repo or
# env var. deps/ is gitignored
.PHONY: vendor vendor-clean
vendor:
	@test -d "$(CLASS_REPO)/libpi" || { echo "ERROR: set CS240LX_2026_PATH to the class repo, or run from inside it"; exit 1; }
	rm -rf deps && mkdir -p deps/lib
	cp -r "$(CLASS_REPO)/libpi" deps/libpi
	cp -r "$(CLASS_REPO)/lib/libm" deps/lib/libm
	-$(MAKE) -s -C deps/libpi clean >/dev/null 2>&1
	@echo "vendored libpi + lib/libm -> deps/  ('make' now builds standalone; keep PRIVATE)"
vendor-clean:
	rm -rf deps
