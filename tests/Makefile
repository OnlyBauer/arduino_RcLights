# Host-side tests.
#
# Three suites, and the split between the first two is the whole point of the
# library's layout:
#
#   core      the real src/rclights_core.c, compiled as C, with no Arduino
#             anywhere near it. Every decision the lights make is tested here.
#   arduino   the real src/RcLights.cpp, compiled as C++ against the mock
#             runtime in arduino_stubs/. Capture, pins, polarity, shutdown.
#   example   the shipping RcLightsCar.ino, compiled as it ships, checking that
#             the settings it lists are still the library's own defaults.
#
#   make              build and run everything
#   make sanitize     the same, under AddressSanitizer and UBSan
#   make clean
#
# The C source is compiled with the C compiler and the wrapper with the C++
# one, which is what the Arduino build does. Compiling the core as C++ is not a
# supported configuration and would not be representative.
#
# Read arduino_stubs/Arduino.h before trusting the arduino suite: there is no
# board here, and the AVR pin-change capture is compiled out entirely.

CC       ?= gcc
CXX      ?= g++
CFLAGS   := -std=c11 -Wall -Wextra -Werror -O2
CXXFLAGS := -std=c++11 -Wall -Wextra -Werror -O2
BUILD    := ../build_tests

SANFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1

CORE_SRC := ../src/rclights_core.c
WRAP_SRC := ../src/RcLights.cpp arduino_stubs/arduino_stubs.cpp

SUITES   := test_core test_arduino_port test_example_defaults

.PHONY: all run sanitize clean
all: run

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/test_core: test_core.c $(CORE_SRC) test_util.h ../src/rclights_core.h | $(BUILD)
	$(CC) $(CFLAGS) -I../src -I. -o $@ test_core.c $(CORE_SRC)

$(BUILD)/test_arduino_port: test_arduino_port.cpp $(WRAP_SRC) $(BUILD)/rclights_core.o | $(BUILD)
	$(CXX) $(CXXFLAGS) -I../src -I. -Iarduino_stubs -o $@ \
	    test_arduino_port.cpp $(WRAP_SRC) $(BUILD)/rclights_core.o

# The shipping example, compiled as it ships. -I puts its directory on the
# include path so that the .ino can be included by name.
EXAMPLE_DIR := ../examples/RcLightsCar

$(BUILD)/test_example_defaults: test_example_defaults.cpp $(EXAMPLE_DIR)/RcLightsCar.ino \
    $(WRAP_SRC) $(BUILD)/rclights_core.o | $(BUILD)
	$(CXX) $(CXXFLAGS) -I../src -I. -Iarduino_stubs -I$(EXAMPLE_DIR) -o $@ \
	    test_example_defaults.cpp $(WRAP_SRC) $(BUILD)/rclights_core.o

$(BUILD)/rclights_core.o: $(CORE_SRC) ../src/rclights_core.h | $(BUILD)
	$(CC) $(CFLAGS) -I../src -c $(CORE_SRC) -o $@

BINS := $(addprefix $(BUILD)/,$(SUITES))

# Each suite has to say how it went rather than only setting an exit status: a
# suite killed by a signal does not reliably report one, and under Cygwin make a
# segfaulting binary arrives as exit 0.
run: $(BINS)
	@fail=0; reported=0; \
	for t in $(BINS); do \
	  out=$$(./$$t 2>&1) || fail=1; \
	  printf '%s\n' "$$out"; \
	  case "$$out" in *PASS*|*FAIL*) reported=$$((reported+1)) ;; esac; \
	done; \
	expected=$(words $(SUITES)); \
	if [ $$reported -ne $$expected ]; then \
	  echo "ONLY $$reported OF $$expected SUITES REPORTED — one died without saying so"; \
	  exit 1; \
	fi; \
	if [ $$fail -ne 0 ]; then echo "SOME TESTS FAILED"; exit 1; fi; \
	echo "All $$expected suites passed."

sanitize: | $(BUILD)
	$(CC) $(CFLAGS) $(SANFLAGS) -I../src -c $(CORE_SRC) -o $(BUILD)/rclights_core.san.o
	$(CC) $(CFLAGS) $(SANFLAGS) -I../src -I. -o $(BUILD)/test_core.san \
	    test_core.c $(CORE_SRC)
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -I../src -I. -Iarduino_stubs \
	    -o $(BUILD)/test_arduino_port.san test_arduino_port.cpp $(WRAP_SRC) \
	    $(BUILD)/rclights_core.san.o
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -I../src -I. -Iarduino_stubs -I$(EXAMPLE_DIR) \
	    -o $(BUILD)/test_example_defaults.san test_example_defaults.cpp $(WRAP_SRC) \
	    $(BUILD)/rclights_core.san.o
	./$(BUILD)/test_core.san
	./$(BUILD)/test_arduino_port.san
	./$(BUILD)/test_example_defaults.san
	@echo "Sanitized suites passed."

clean:
	rm -rf $(BUILD)
