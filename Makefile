BUILD_DIR=build
ifneq ($(V),1)
  Q := @
endif

default: $(BUILD_DIR)/bin/auto_check

CFLAGS += -g -Wall -Wextra -Werror $(shell pkg-config --cflags json-c)
LDFLAGS += -lutil $(shell pkg-config --libs json-c)

$(BUILD_DIR)/obj/%.o: src/%.c
	$(Q)echo Building object $@
	$(Q)mkdir -p $(@D)
	$(Q)$(TOOLSET)gcc -x c -c $< -o $@ $(CFLAGS)

$(BUILD_DIR)/bin/%:
	$(Q)echo Building binary $@
	$(Q)mkdir -p $(@D)
	$(Q)$(TOOLSET)gcc -o $@ $^ ${LDFLAGS}

$(BUILD_DIR)/bin/auto_check: $(BUILD_DIR)/obj/main.o
