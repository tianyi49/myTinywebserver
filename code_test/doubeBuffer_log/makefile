CXX ?= g++
DEBUG ?= 1

ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2
endif

logtest: logtest.cpp logger.cpp
	$(CXX) -o logtest $^ $(CXXFLAGS) -lpthread

clean:
	rm -r logtest
