# ChatterBox Makefile
CXX := clang++
CXXFLAGS := -std=c++17 -Wall -Wextra -pedantic -O2 -g
INCLUDES := -I./include

# Source directories
SRC_DIR := src
IPC_SRC := $(SRC_DIR)/ipc
PROTOCOL_SRC := $(SRC_DIR)/protocol
SYNC_SRC := $(SRC_DIR)/sync
SERVER_SRC := $(SRC_DIR)/server
CLIENT_SRC := $(SRC_DIR)/client

# Build directory
BUILD_DIR := build

# Source files
IPC_SRCS := $(wildcard $(IPC_SRC)/*.cpp)
PROTOCOL_SRCS := $(wildcard $(PROTOCOL_SRC)/*.cpp)
SYNC_SRCS := $(wildcard $(SYNC_SRC)/*.cpp)
SERVER_SRCS := $(wildcard $(SERVER_SRC)/*.cpp)
CLIENT_SRCS := $(wildcard $(CLIENT_SRC)/*.cpp)

# Object files
IPC_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(IPC_SRCS))
PROTOCOL_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(PROTOCOL_SRCS))
SYNC_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SYNC_SRCS))
SERVER_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SERVER_SRCS))
CLIENT_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(CLIENT_SRCS))

# Common objects (used by both server and client)
COMMON_OBJS := $(IPC_OBJS) $(PROTOCOL_OBJS) $(SYNC_OBJS)

# Libraries
LIBS := -lpthread

# Targets
SERVER := $(BUILD_DIR)/chatterbox_server
CLIENT := $(BUILD_DIR)/chatterbox_client

.PHONY: all clean dirs server client

all: dirs server client

dirs:
	@mkdir -p $(BUILD_DIR)/ipc $(BUILD_DIR)/protocol $(BUILD_DIR)/sync $(BUILD_DIR)/server $(BUILD_DIR)/client

server: dirs $(SERVER)

client: dirs $(CLIENT)

$(SERVER): $(COMMON_OBJS) $(SERVER_OBJS) $(BUILD_DIR)/server_main.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBS)

$(CLIENT): $(COMMON_OBJS) $(CLIENT_OBJS) $(BUILD_DIR)/client_main.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBS) -lncurses

$(BUILD_DIR)/server_main.o: apps/server_main.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/client_main.o: apps/client_main.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

# Print info
info:
	@echo "IPC sources: $(IPC_SRCS)"
	@echo "Protocol sources: $(PROTOCOL_SRCS)"
	@echo "Sync sources: $(SYNC_SRCS)"
	@echo "Server sources: $(SERVER_SRCS)"
	@echo "Client sources: $(CLIENT_SRCS)"
