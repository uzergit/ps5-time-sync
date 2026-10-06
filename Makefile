PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := time-sync.elf

CFLAGS := -Wall -Werror -O2
# Override the default server at build time, e.g.:
#   make NTP_SERVER=192.168.1.1
ifdef NTP_SERVER
CFLAGS += -DNTP_SERVER=\"$(NTP_SERVER)\"
endif

all: $(ELF)

$(ELF): main.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(ELF)

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^
