#include "Scenegraph.h"
#include "ScenegraphStructs.h"
#include <memory>
#include <string>

using namespace sgraph;

void Scenegraph::makeScenegraph(std::shared_ptr<INode> root)
{
    this->root = root;
}

void Scenegraph::addNode(const std::string name, std::shared_ptr<INode> node)
{
    nodes[name] = node;
}

std::shared_ptr<INode> Scenegraph::getRoot()
{
    return root;
}

void Scenegraph::loadScenegraph(std::string filePath)
{
}