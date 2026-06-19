// ToolRegistry.h — araç listesini üretir. Yeni araç = tek push_back.
#pragma once

#include "ITool.h"
#include "AdQueryTool.h"

#include <memory>
#include <vector>

namespace ith {

class ToolRegistry {
public:
    static std::vector<std::unique_ptr<ITool>> CreateTools() {
        std::vector<std::unique_ptr<ITool>> tools;
        tools.push_back(std::make_unique<AdQueryTool>());
        // Yeni araçlar buraya eklenir:
        // tools.push_back(std::make_unique<BaskaArac>());
        return tools;
    }
};

} // namespace ith
