CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined

SIBLOCK_SRC = $(wildcard ../lib/siblock_core/*.cpp)
SIBLOCK_HDR = $(wildcard ../lib/siblock_core/*.h)

test: test_core test_siblock
	./test_core
	./test_siblock

test_core: test_core.cpp ../lib/adblock_core/adblock_core.cpp ../lib/adblock_core/adblock_core.h
	$(CXX) $(CXXFLAGS) -o $@ test_core.cpp ../lib/adblock_core/adblock_core.cpp

test_siblock: test_siblock.cpp $(SIBLOCK_SRC) $(SIBLOCK_HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_siblock.cpp $(SIBLOCK_SRC)

clean:
	rm -f test_core test_siblock

.PHONY: test clean
