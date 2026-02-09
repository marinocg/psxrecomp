.PHONY: configure build test lint clean rebuild

BUILD_DIR ?= build

configure:
	cmake -S . -B $(BUILD_DIR) -G Ninja -DBUILD_TESTS=ON -DBUILD_CLI=ON

build: configure
	cmake --build $(BUILD_DIR)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

lint:
	git ls-files '*.cpp' '*.h' | xargs -r clang-format --dry-run --Werror

clean:
	cmake --build $(BUILD_DIR) --target clean

rebuild: clean build
