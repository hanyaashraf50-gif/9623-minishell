CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -g
TARGET   = myshell
SOURCES  = command.cc tokenizer.cc
HEADERS  = command.h tokenizer.h

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

clean:
	rm -f $(TARGET) *.o myshell.log

.PHONY: all clean