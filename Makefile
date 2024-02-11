.PHONY: build install uninstall-usr

init-debug:
	mkdir -p build
	cd build; \
	cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DCMAKE_BUILD_TYPE=Debug ..; \

init-debug-usr:
	mkdir -p build
	cd ./build; \
	cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DCMAKE_BUILD_TYPE=Debug ..

init-release-usr:
	mkdir -p build
	cd ./build; \
	cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DCMAKE_BUILD_TYPE=Release ..

init-release-local:
	mkdir -p build
	cd ./build; \
	cmake -DCMAKE_INSTALL_PREFIX=./binary -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DCMAKE_BUILD_TYPE=Release ..

build:
	cmake --build ./build

install:
	sudo cmake --install ./build

install-local:
	cmake --install ./build

uninstall-usr:
	sudo rm -f /usr/lib/fcitx5/libbridge.so
	sudo rm -f /usr/lib/fcitx5/libbridge-fcp.so
	sudo rm -f /usr/lib/fcitx5/libbridge-fcn.so
	sudo rm -f /usr/share/fcitx5/addon/bridge.conf
	sudo rm -f /usr/share/fcitx5/addon/bridge-fcp.conf
	sudo rm -f /usr/share/fcitx5/addon/bridge-fcn.conf
	sudo rm -f /usr/share/fcitx5/inputmethod/bridge.conf
	sudo rm -f /usr/share/fcitx5/inputmethod/bridge-fcp.conf
	sudo rm -f /usr/share/fcitx5/inputmethod/bridge-fcn.conf

proto:
	protoc --cpp_out=. msgs.proto

clean:
	rm -rf build