BUILD = build
PREFIX ?= /usr/local

all:
	@cmake -B $(BUILD) -DCMAKE_BUILD_TYPE=RelWithDebInfo
	@cmake --build $(BUILD) -j

release:
	@cmake -B $(BUILD) -DCMAKE_BUILD_TYPE=Release
	@cmake --build $(BUILD) -j
	@strip $(BUILD)/dawn

debug:
	@cmake -B $(BUILD) -DCMAKE_BUILD_TYPE=Debug
	@cmake --build $(BUILD) -j

with-ai:
	@cmake -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DUSE_LIBAI=ON
	@cmake --build $(BUILD) -j

web:
	@emcmake cmake -B $(BUILD)-web -DCMAKE_BUILD_TYPE=Release
	@cmake --build $(BUILD)-web -j

clean:
	@rm -rf $(BUILD) $(BUILD)-web

install: release
	@cmake --install $(BUILD) --prefix $(PREFIX)

uninstall:
	@rm -f $(PREFIX)/bin/dawn

.PHONY: all release debug with-ai web clean install uninstall
