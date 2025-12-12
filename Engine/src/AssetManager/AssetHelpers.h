#pragma once

#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <string>

#include "Logging/Log.h"

#include <toml++/toml.hpp>

namespace Rapture {

// Helper function to find related shader file paths
inline std::optional<std::filesystem::path> getRelatedShaderPath(const std::filesystem::path &basePath,
                                                                 const std::string &targetStage)
{
    if (!std::filesystem::exists(basePath)) {
        RP_CORE_WARN("Base path does not exist: {}", basePath.string());
        return std::nullopt;
    }

    std::string basePathStr = basePath.string();
    // Regex to capture: (base_name)(.stage)(.extension)
    // Example: "path/to/MyShader.vert.glsl" ->
    // ("path/to/MyShader")(".vert")(".glsl")
    std::regex pathRegex("^(.*?)(\\.(?:vert|vs|frag|fs|geom|gs|comp|cs))(\\.[^.]+)$");
    std::smatch match;

    if (!std::regex_match(basePathStr, match, pathRegex) || match.size() != 4) {
        RP_CORE_WARN("Could not parse base shader "
                     "path structure: {}. Expected format like 'name.stage.ext'.",
                     basePathStr);
        return std::nullopt;
    }

    std::string baseName = match[1].str();
    std::string finalExt = match[3].str();

    const std::map<std::string, std::array<std::string, 2>> stageExtensions = {{"vertex", {".vert", ".vs"}},
                                                                               {"fragment", {".frag", ".fs"}},
                                                                               {"geometry", {".geom", ".gs"}},
                                                                               {"compute", {".comp", ".cs"}}};

    if (stageExtensions.find(targetStage) == stageExtensions.end()) {
        RP_CORE_ERROR("Invalid target shader "
                      "stage requested: {}",
                      targetStage);
        return std::nullopt;
    }

    for (const auto &ext : stageExtensions.at(targetStage)) {
        std::filesystem::path potentialPath = baseName + ext + finalExt;
        if (std::filesystem::exists(potentialPath)) {
            return potentialPath;
        }
    }

    RP_CORE_WARN("Could not find related {} shader for base path: {}", targetStage, basePath.string());
    return std::nullopt;
}

// uses toml parser to get the cubemap paths from a .cubemap file
inline std::vector<std::string> getCubemapPaths(const std::filesystem::path &basePath)
{
    std::vector<std::string> cubemapPaths;
    cubemapPaths.reserve(6); // Reserve space for 6 faces

    try {
        auto config = toml::parse_file(basePath.string());

        // Check if paths section exists
        auto paths_table = config["paths"];
        if (!paths_table.is_table()) {
            RP_CORE_ERROR("Cubemap file '{}' does not contain a valid 'paths' section.", basePath.string());
            return cubemapPaths;
        }

        // Check if paths are relative to the cubemap file
        bool relative = false;
        if (auto rel_val = paths_table["relative"].value<bool>()) {
            relative = *rel_val;
        }

        // Get the directory of the cubemap file for relative path resolution
        std::filesystem::path cubemapDir = basePath.parent_path();

        // Define the order of cubemap faces: right(+X), left(-X), top(+Y), bottom(-Y), front(+Z), back(-Z)
        const std::array<std::string, 6> faceNames = {"right", "left", "top", "bottom", "front", "back"};

        for (const auto &faceName : faceNames) {
            if (auto face_path = paths_table[faceName].value<std::string>()) {
                std::string finalPath = *face_path;

                // If paths are relative, resolve them relative to the cubemap file
                if (relative) {
                    std::filesystem::path resolvedPath = cubemapDir / finalPath;
                    finalPath = resolvedPath.string();
                }

                cubemapPaths.push_back(finalPath);
            } else {
                RP_CORE_ERROR("Cubemap file '{}' is missing '{}' face path.", basePath.string(), faceName);
                return {}; // Return empty vector on error
            }
        }

    } catch (const toml::parse_error &err) {
        RP_CORE_ERROR("Failed to parse cubemap file '{}':\n{}", basePath.string(), err.what());
    }

    return cubemapPaths;
}

} // namespace Rapture
