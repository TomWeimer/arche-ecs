BUILD_DIR ?= build
BUILD_TYPE ?= Release
NUM_JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 8)

.PHONY: all build test benchmark clean fclean re lint

all: build

lint:
	@python3 scripts/check_architecture.py
	@ninja -C $(BUILD_DIR)

$(BUILD_DIR)/CMakeCache.txt:
	@cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

build: $(BUILD_DIR)/CMakeCache.txt
	@cmake --build $(BUILD_DIR) -j$(NUM_JOBS)

test: build
	@ctest --test-dir $(BUILD_DIR) --output-on-failure

benchmark: build
	@./$(BUILD_DIR)/arche_benchmarks
	@./$(BUILD_DIR)/comparative_ecs_benchmarks

clean:
	@if [ -d "$(BUILD_DIR)" ]; then cmake --build $(BUILD_DIR) --target clean; fi

fclean:
	@rm -rf $(BUILD_DIR)

re: fclean all
