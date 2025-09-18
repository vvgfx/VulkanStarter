CXX = g++
CXXFLAGS = -std=c++20 -O2 -g
SLANGC = slangc.exe
VULKAN_PATH = $(VULKAN_SDK)
INCLUDES = -I"$(VULKAN_PATH)/Include" -I../lib/glfw/include -I../lib/glm -I../include
LIB_DIRS = -L"$(VULKAN_PATH)/Lib" -L../lib/glfw/lib-mingw-w64
SHADERS_DIR = shaders
SHADER_SOURCES = shader.slang
SHADER_OUTPUT = $(SHADERS_DIR)/slang.spv
LIBS = -lglfw3 -lvulkan-1 -lgdi32 -limm32
TARGET = VulkanApp

$(TARGET): main.cpp ../include/vulkan_pch.h.gch $(SHADER_OUTPUT)
	$(CXX) $(CXXFLAGS) $(INCLUDES) main.cpp $(LIB_DIRS) $(LIBS) -o $(TARGET)

../include/vulkan_pch.h.gch: ../include/vulkan_pch.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) ../include/vulkan_pch.h -o ../include/vulkan_pch.h.gch

$(SHADERS_DIR):
	mkdir -p $(SHADERS_DIR)

$(SHADER_OUTPUT): $(SHADER_SOURCES) | $(SHADERS_DIR)
	$(SLANGC) $(SHADER_SOURCES) -target spirv -profile spirv_1_4 -emit-spirv-directly \
		-fvk-use-entrypoint-name -entry vertMain -entry fragMain -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(SHADER_OUTPUT)

nuke:
	rm -f $(TARGET) ../include/vulkan_pch.h.gch $(SHADER_OUTPUT)
	rm -rf $(SHADERS_DIR)

.PHONY: run clean nuke