# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 YUjinEDU

CXX ?= g++
CXXFLAGS ?= -O3 -Wall -Wextra -std=c++17 -fPIC -MMD -MP -Iinclude -Ithird_party -Ithird_party/cpp-httplib -I/usr/include/opencv4 -I/usr/src/jetson_multimedia_api/include
LDFLAGS ?= -L/usr/lib/aarch64-linux-gnu/nvidia

OPENCV_LIBS ?= -l:libopencv_core.so.406 -l:libopencv_imgproc.so.406 -l:libopencv_imgcodecs.so.406
ARGUS_LIBS ?= -lnvargus -lnvbufsurface -lEGL

BIN_DIR := bin
BUILD_DIR := build

all: $(BIN_DIR)/libstudioclient.so $(BIN_DIR)/studio $(BIN_DIR)/test_shm

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Client shared library (lightweight, no Argus/OpenCV deps, only pthread + rt)
$(BIN_DIR)/libstudioclient.so: src/client.cpp include/studio/client.h include/studio/shm_protocol.h | $(BIN_DIR)
	$(CXX) -O3 -Wall -Wextra -std=c++17 -fPIC -shared -Iinclude src/client.cpp -o $@ -pthread -lrt

# SHM test binary
$(BIN_DIR)/test_shm: tests/test_shm.cpp $(BIN_DIR)/libstudioclient.so | $(BIN_DIR)
	$(CXX) -O3 -Wall -Wextra -std=c++17 -Iinclude tests/test_shm.cpp -o $@ -L$(BIN_DIR) -lstudioclient -Wl,-rpath,'$$ORIGIN' -pthread -lrt

# Studio main service
STUDIO_SRCS := src/main.cpp src/camera_service.cpp src/display_pipeline.cpp src/http_server.cpp src/preset_manager.cpp src/client.cpp
STUDIO_OBJS := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(STUDIO_SRCS))
STUDIO_DEPS := $(STUDIO_OBJS:.o=.d)


$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN_DIR)/studio: $(STUDIO_OBJS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $(ARGUS_LIBS) $(OPENCV_LIBS) -ljpeg -pthread -lrt
-include $(STUDIO_DEPS)

test: $(BIN_DIR)/test_shm
	./$(BIN_DIR)/test_shm

clean:
	rm -rf $(BIN_DIR) $(BUILD_DIR)

.PHONY: all test clean
