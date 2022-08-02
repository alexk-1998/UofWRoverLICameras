
# Clear the flags from env
CPPFLAGS :=
LDFLAGS :=

# c++ compiler
CPP := g++

# Use absolute path for better access from everywhere
TOP_DIR 	:= $(shell pwd)
SRC_DIR 	:= $(TOP_DIR)/src
OBJ_DIR		:= $(TOP_DIR)/obj
COMMON_DIR	:= $(SRC_DIR)/common
CAPTURE_DIR	:= $(SRC_DIR)/stream_capture
PREVIEW_DIR	:= $(SRC_DIR)/stream_preview
SC			:= StreamCapture
SP			:= StreamPreview
SC_APP 		:= $(TOP_DIR)/$(SC)
SP_APP 		:= $(TOP_DIR)/$(SP)

# All common header files
CPPFLAGS += -std=c++11 \
	-I"$(TOP_DIR)/include" \
	-I"$(TOP_DIR)/include/Argus" \
	-I"$(TOP_DIR)/include/EGLStream" \
	-I"$(TOP_DIR)/include/libjpeg-8b" \
	-I"/usr/include/libdrm" \
	-I"/usr/include/opencv4"

# All dependent libraries
LDFLAGS += \
	-lpthread \
	-lv4l2 \
	-lEGL \
	-lGLESv2 \
	-lX11 \
	-lnvbuf_utils \
	-lnvjpeg \
	-ldrm \
	-lopencv_highgui \
	-lopencv_imgproc \
	-lopencv_core \
	-lnvargus_socketclient \
	-L"/usr/lib/aarch64-linux-gnu/tegra"

# sources for each executable
COMMON_SRCS := $(wildcard $(COMMON_DIR)/*.cpp)
CAPTURE_SRCS := $(wildcard $(CAPTURE_DIR)/*.cpp)
PREVIEW_SRCS := $(wildcard $(PREVIEW_DIR)/*.cpp)

# objects for each executable
COMMON_OBJS := $(COMMON_SRCS:$(COMMON_DIR)/%.cpp=$(OBJ_DIR)/%.o)
CAPTURE_OBJS := \
	$(COMMON_OBJS) \
	$(CAPTURE_SRCS:$(CAPTURE_DIR)/%.cpp=$(OBJ_DIR)/%.o)
PREVIEW_OBJS := \
	$(COMMON_OBJS) \
	$(PREVIEW_SRCS:$(PREVIEW_DIR)/%.cpp=$(OBJ_DIR)/%.o)

# recipes

all: $(SC_APP) $(SP_APP)

$(SC_APP): $(CAPTURE_OBJS)
	@echo "Linking: $@"
	@$(CPP) -o $@ $(CAPTURE_OBJS) $(CPPFLAGS) $(LDFLAGS)

$(SP_APP): $(PREVIEW_OBJS)
	@echo "Linking: $@"
	@$(CPP) -o $@ $(PREVIEW_OBJS) $(CPPFLAGS) $(LDFLAGS)

$(OBJ_DIR)/%.o: $(COMMON_DIR)/%.cpp | $(OBJ_DIR)
	@echo "Compiling: $<"
	@$(CPP) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(CAPTURE_DIR)/%.cpp | $(OBJ_DIR)
	@echo "Compiling: $<"
	@$(CPP) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(PREVIEW_DIR)/%.cpp | $(OBJ_DIR)
	@echo "Compiling: $<"
	@$(CPP) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $@

clean:
	rm -rf $(HOME)/$(SC) $(HOME)/$(SP)
	rm -rf $(SC_APP) $(SP_APP) $(OBJ_DIR)

install:
	rm -rf $(HOME)/$(SC) $(HOME)/$(SP)
	ln -s $(SC_APP) $(HOME)/$(SC)
	ln -s $(SP_APP) $(HOME)/$(SP)
