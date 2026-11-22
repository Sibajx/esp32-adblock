# Simulador del ESP32 en Linux: compila src/ sin cambios contra una capa de Arduino falsa.
#   make            -> ./esp32-sim (con sim/secrets.h) y ./esp32-sim-fresh (sin secrets.h,
#                      como el firmware publicado)
#   make run        -> DNS en 127.0.0.1:5353, panel en http://127.0.0.1:8080/
BUILD = build
BUILD_FRESH = build-fresh
SRC = $(filter-out ../src/secrets.h,$(wildcard ../src/*.cpp ../src/*.h))
LIBS = ../lib/adblock_core/adblock_core.cpp $(wildcard ../lib/siblock_core/*.cpp)
LIB_DEPS = ../lib/adblock_core/* ../lib/siblock_core/*
CXXFLAGS = -std=gnu++17 -O1 -g -Wall -Wno-unused-function -fsanitize=address,undefined -pthread \
  -Ishim -I../lib/adblock_core -I../lib/siblock_core

all: esp32-sim esp32-sim-fresh

esp32-sim: $(SRC) shim/*.h shim/*.cpp sim_main.cpp secrets.h $(LIB_DEPS)
	rm -rf $(BUILD)/src && mkdir -p $(BUILD)/src
	cp $(SRC) $(BUILD)/src/ && cp secrets.h $(BUILD)/src/
	$(CXX) $(CXXFLAGS) -I$(BUILD)/src -o $@ $(BUILD)/src/*.cpp shim/shim.cpp sim_main.cpp $(LIBS)

esp32-sim-fresh: $(SRC) shim/*.h shim/*.cpp sim_main.cpp $(LIB_DEPS)
	rm -rf $(BUILD_FRESH)/src && mkdir -p $(BUILD_FRESH)/src
	cp $(SRC) $(BUILD_FRESH)/src/
	$(CXX) $(CXXFLAGS) -I$(BUILD_FRESH)/src -o $@ $(BUILD_FRESH)/src/*.cpp shim/shim.cpp sim_main.cpp $(LIBS)

run: esp32-sim
	SIM_FLASH=./sim-flash ./esp32-sim

clean:
	rm -rf $(BUILD) $(BUILD_FRESH) esp32-sim esp32-sim-fresh sim-flash

.PHONY: all run clean
