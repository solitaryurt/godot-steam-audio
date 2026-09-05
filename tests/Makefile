# Standalone Linux SDK tests, without compiling Godot or changing the main build.
CXX ?= c++
SDK := ../src/lib/steamaudio
CPPFLAGS += -I../src -isystem $(SDK)/include
CXXFLAGS ?= -O0 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -pthread
LDLIBS += -L$(SDK)/lib/linux-x64 -Wl,-rpath,$(abspath $(SDK)/lib/linux-x64) -lphonon
CORE := ../src/probe_core.cpp

.PHONY: all check
all: probe_batch_test probe_bake_failure_test probe_bake_cancel_test reflection_transition_test

probe_batch_test: probe_batch_test.cpp $(CORE) ../src/probe_core.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) probe_batch_test.cpp $(CORE) $(LDLIBS) -o $@

probe_bake_failure_test: probe_bake_failure_test.cpp $(CORE) ../src/probe_core.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) probe_bake_failure_test.cpp $(CORE) $(LDLIBS) -Wl,--wrap=iplPathBakerBake -Wl,--wrap=iplReflectionsBakerBake -o $@

probe_bake_cancel_test: probe_bake_cancel_test.cpp $(CORE) ../src/probe_core.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) probe_bake_cancel_test.cpp $(CORE) $(LDLIBS) -Wl,--wrap=iplPathBakerCancelBake -Wl,--wrap=iplReflectionsBakerCancelBake -o $@

reflection_transition_test: reflection_transition_test.cpp $(CORE) ../src/probe_core.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) reflection_transition_test.cpp $(CORE) $(LDLIBS) -o $@

check: all
	./probe_batch_test
	./probe_bake_failure_test
	./probe_bake_cancel_test
	./reflection_transition_test
