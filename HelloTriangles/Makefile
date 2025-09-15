CXX = g++
CXXFLAGS = -std=c++20 -O2 -g

VULKAN_PATH = $(VULKAN_SDK)
INCLUDES = -I"$(VULKAN_PATH)/Include" -I../lib/glfw/include -I../lib/glm -I../include
LIB_DIRS = -L"$(VULKAN_PATH)/Lib" -L../lib/glfw/lib-mingw-w64


LIBS = -lglfw3 -lvulkan-1 -lgdi32 -limm32


TARGET = VulkanApp

$(TARGET): main.cpp ../include/vulkan_pch.h.gch
	$(CXX) $(CXXFLAGS) $(INCLUDES) main.cpp $(LIB_DIRS) $(LIBS) -o $(TARGET)

# Should not need this more than once, but adding this here anyways
../include/vulkan_pch.h.gch: ../include/vulkan_pch.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) ../include/vulkan_pch.h -o ../include/vulkan_pch.h.gch

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

nuke:
	rm -f $(TARGET) ../include/vulkan_pch.h.gch

.PHONY: run clean