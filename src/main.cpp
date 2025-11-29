#include <PBREngine.h>

int main(int argc, char *argv[])
{

    // switch from vulkan engine to pbr engine for inheritance hierarchy
    PBREngine engine;

    engine.init();

    engine.run();

    engine.cleanup();

    return 0;
}
