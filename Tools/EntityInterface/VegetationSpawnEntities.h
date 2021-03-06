// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>

namespace SceneEngine { class VegetationSpawnManager; }
namespace EntityInterface
{
    class RetainedEntities;
    void RegisterVegetationSpawnFlexObjects(
        RetainedEntities& flexSys, 
        std::shared_ptr<SceneEngine::VegetationSpawnManager> spawnManager);
}
