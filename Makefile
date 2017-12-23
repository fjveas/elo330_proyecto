#
# 'make'        build executable file
# 'make clean'  removes all .o and executable files
#

# Executables
APP_NAME = sdrrc

# C compiler to use
#CC = clang
CC = gcc

# Compile-time flags
CFLAGS  = -std=gnu11 -W -Wall -O2 -pipe -fomit-frame-pointer
CFLAGS += -Wno-missing-field-initializers
CFLAGS += -Wno-unused-parameter
CFLAGS += -Wno-unused-function
#CFLAGS += -Wno-unused-variable

# Define any libraries to link into executable:
LDFLAGS  = -lm -lrt
LDFLAGS += -lpthread
LDFLAGS += -s

# Define C source files
SOURCES = $(wildcard src/*.c)

# Objects
OBJECTS = $(addprefix obj/,$(notdir $(SOURCES:%.c=%.o)))

# Dependencies
DEPS = $(OBJECTS:%.o=%.d)

.PHONY: clean directories

all: directories executables

directories: obj

executables: $(APP_NAME)

obj:
	mkdir -p obj

$(APP_NAME): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@ $(LDFLAGS)

-include $(DEPS)

obj/%.o: src/%.c
	$(CC) $(CFLAGS) -MMD -c $< -o $@

clean:
	$(RM) obj/*.o obj/*.d src/*~ $(APP_NAME)

