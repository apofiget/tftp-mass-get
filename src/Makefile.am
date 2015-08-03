
CFLAGS := $(filter-out -O2,$(CFLAGS))
CFLAGS := $(filter-out -g,$(CFLAGS))

AM_CFLAGS = -O3 -Wall -Wextra -D_REENTERANT -std=gnu99 -pedantic

bin_PROGRAMS = tftp_mass_get
tftp_mass_get_SOURCES = tftp-get.c
