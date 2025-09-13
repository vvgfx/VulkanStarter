CXX = g++
CXXFLAGS = -std=c++17 -O2 -g

VULKAN_PATH = $(VULKAN_SDK)
INCLUDES = -I"$(VULKAN_PATH)/Include" -I../lib/glfw/include -I../lib/glm
LIB_DIRS = -L"$(VULKAN_PATH)/Lib" -L../lib/glfw/lib-mingw-w64


LIBS = -lglfw3 -lvulkan-1 -lgdi32 -limm32


TARGET = VulkanApp

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) main.cpp $(LIB_DIRS) $(LIBS) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: run clean