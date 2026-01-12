# ==============================================================================
# WebServer Makefile - Enterprise Level Build System
# ==============================================================================

PROJECT_NAME := webserver
VERSION := 1.0.0

# Directories
SRC_DIR := src
INC_DIR := include
BUILD_DIR := build
BIN_DIR := bin
OBJ_DIR := $(BUILD_DIR)/obj
DEP_DIR := $(BUILD_DIR)/deps

# Output
TARGET := $(BIN_DIR)/$(PROJECT_NAME)

# Compiler
CXX := g++
CXXFLAGS := -std=c++11 -Wall -Wextra -Wno-unused-parameter -I$(INC_DIR)
LDFLAGS := -lpthread -lmysqlclient

# Build mode
DEBUG ?= 0
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g -O0 -DDEBUG
    BUILD_MODE := DEBUG
else
    CXXFLAGS += -O2 -DNDEBUG
    BUILD_MODE := RELEASE
endif

# Source files
SRCS := $(SRC_DIR)/core/main.cpp \
        $(SRC_DIR)/core/config.cpp \
        $(SRC_DIR)/core/webserver.cpp \
        $(SRC_DIR)/http/http_connection.cpp \
        $(SRC_DIR)/log/log.cpp \
        $(SRC_DIR)/timer/timer_list.cpp \
        $(SRC_DIR)/database/database_pool.cpp

# Object files
OBJS := $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

# Colors
COLOR_RESET := \033[0m
COLOR_GREEN := \033[32m
COLOR_BLUE := \033[34m
COLOR_YELLOW := \033[33m

# ==============================================================================
# Main targets
# ==============================================================================

.PHONY: all clean distclean rebuild run start stop status test help info

all: info $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	@echo "$(COLOR_GREEN)[LINK]$(COLOR_RESET) Linking $(PROJECT_NAME)..."
	@$(CXX) $(OBJS) -o $@ $(LDFLAGS)
	@echo "$(COLOR_GREEN)[SUCCESS]$(COLOR_RESET) Build complete: $@"

# Pattern rule for object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@echo "$(COLOR_BLUE)[CC]$(COLOR_RESET) $<"
	@mkdir -p $(dir $@)
	@mkdir -p $(DEP_DIR)/$(dir $*)
	@$(CXX) $(CXXFLAGS) -MMD -MP -MF $(DEP_DIR)/$*.d -c $< -o $@

# Create directories
$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p $@
	@mkdir -p $(OBJ_DIR)/core $(OBJ_DIR)/http $(OBJ_DIR)/log
	@mkdir -p $(OBJ_DIR)/timer $(OBJ_DIR)/database
	@mkdir -p $(DEP_DIR)/core $(DEP_DIR)/http $(DEP_DIR)/log
	@mkdir -p $(DEP_DIR)/timer $(DEP_DIR)/database

clean:
	@echo "$(COLOR_YELLOW)[CLEAN]$(COLOR_RESET) Removing build artifacts..."
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "$(COLOR_GREEN)[SUCCESS]$(COLOR_RESET) Clean complete"

distclean: clean
	@echo "$(COLOR_YELLOW)[DISTCLEAN]$(COLOR_RESET) Deep clean..."
	@rm -f *.log *ServerLog nohup.out
	@echo "$(COLOR_GREEN)[SUCCESS]$(COLOR_RESET) Deep clean complete"

rebuild: clean all

run: $(TARGET)
	@echo "$(COLOR_GREEN)[RUN]$(COLOR_RESET) Starting server..."
	@$(TARGET) -p 9006

start: $(TARGET)
	@./scripts/manage.sh start

stop:
	@./scripts/manage.sh stop

restart: stop
	@sleep 1
	@$(MAKE) start

status:
	@./scripts/manage.sh status

test:
	@echo "$(COLOR_BLUE)[TEST]$(COLOR_RESET) Running benchmark..."
	@cd tests/benchmark/webbench-1.5 && $(MAKE) && \
		./webbench -c 1000 -t 10 http://localhost:9006/

install-deps:
	@echo "$(COLOR_BLUE)[DEPS]$(COLOR_RESET) Installing dependencies..."
	@sudo apt-get update && sudo apt-get install -y build-essential libmysqlclient-dev mysql-server

info:
	@echo "========================================"
	@echo "  $(PROJECT_NAME) v$(VERSION)"
	@echo "  Mode: $(COLOR_YELLOW)$(BUILD_MODE)$(COLOR_RESET)"
	@echo "========================================"

help:
	@echo "========================================"
	@echo "  WebServer Makefile"
	@echo "========================================"
	@echo "Build:"
	@echo "  make              - Build (DEBUG mode)"
	@echo "  make DEBUG=0      - Build (RELEASE mode)"
	@echo "  make clean        - Clean build files"
	@echo "  make rebuild      - Clean + build"
	@echo ""
	@echo "Run:"
	@echo "  make run          - Build and run"
	@echo "  make start        - Start server"
	@echo "  make stop         - Stop server"
	@echo "  make status       - Check status"
	@echo ""
	@echo "Development:"
	@echo "  make test         - Run benchmark"
	@echo "  make install-deps - Install deps"
	@echo "========================================"

-include $(DEPS)
