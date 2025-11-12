#pragma once
#include "IScenegraph.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace sgraph
{
    class Scenegraph : public IScenegraph
    {
      public:
        virtual void makeScenegraph(std::shared_ptr<INode> root) override;

        virtual void addNode(const std::string name, std::shared_ptr<INode> node) override;

        virtual std::shared_ptr<INode> getRoot() override;

        // virtual std::unordered_map<std::string, std::shared_ptr<INode>> getNodes() override;

        void loadScenegraph(std::string filePath);

      private:
        std::shared_ptr<INode> root;
        std::unordered_map<std::string, std::shared_ptr<INode>> nodes;
    };

} // namespace sgraph