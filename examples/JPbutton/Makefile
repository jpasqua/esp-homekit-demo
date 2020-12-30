PROGRAM = button

EXTRA_COMPONENTS = \
	extras/http-parser \
	extras/dhcpserver \
	$(abspath ../../components/esp8266-open-rtos/wifi_config) \
	$(abspath ../../components/common/button) \
	$(abspath ../../components/esp8266-open-rtos/cJSON) \
	$(abspath ../../components/common/wolfssl) \
	$(abspath ../../components/common/homekit)

FLASH_SIZE ?= 4

EXTRA_CFLAGS += -I../.. -DHOMEKIT_SHORT_APPLE_UUIDS \
				-DDEV_SERIAL=$(DEV_SERIAL)			\
				-DDEV_PASS="$(DEV_PASS)"			\
				-DDEV_SETUP=$(DEV_SETUP)			\
				-DDEV_NAME=$(DEV_NAME)

include $(SDK_PATH)/common.mk

monitor:
	$(FILTEROUTPUT) --port $(ESPPORT) --baud 115200 --elf $(PROGRAM_OUT)


QRTOOL = $(abspath ../../components/common/homekit/tools/gen_qrcode)
.PHONY: qrcode
qrcode:
	$(QRTOOL) $(CAT_ID) "$(DEV_PASS)" "$(DEV_SETUP)" $(DEV_NAME).png
