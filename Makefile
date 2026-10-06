PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := time-sync.elf
UNSYNC_ELF := time-unsync.elf

VERSION ?= dev
UPDATE_REPO ?= uzergit/ps5-time-sync

CFLAGS := -Wall -Werror -O2
CFLAGS += -DVERSION=\"$(VERSION)\" -DUPDATE_REPO=\"$(UPDATE_REPO)\"
# Override the default server at build time, e.g.:
#   make NTP_SERVER=192.168.1.1
ifdef NTP_SERVER
CFLAGS += -DNTP_SERVER=\"$(NTP_SERVER)\"
endif

all: $(ELF) $(UNSYNC_ELF)

$(ELF): main.c
	$(CC) $(CFLAGS) -o $@ $^

# time-unsync also reads etaHEN payloads, which needs an LZMA decoder
# (LZMA SDK by Igor Pavlov, public domain, in lzma/).
UNSYNC_SRC := main.c etahen_scan.c lzma/LzmaDec.c

$(UNSYNC_ELF): $(UNSYNC_SRC) etahen_scan.h
	$(CC) $(CFLAGS) -DUNSYNC -Ilzma -o $@ $(UNSYNC_SRC)

clean:
	rm -f $(ELF) $(UNSYNC_ELF)

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^
