#pragma once
#include <string>
#include <filesystem>

namespace Crescendo {
    class EnginePaths {
    public:
        // Set the active project root (e.g. from command line `-project`)
        static void SetProjectRoot(const std::string& root) {
            s_projectRoot = root;
            if (!s_projectRoot.empty() && s_projectRoot.back() != '/' && s_projectRoot.back() != '\\') {
                s_projectRoot += "/";
            }
        }

        static const std::string& GetProjectRoot() { return s_projectRoot; }

        // Directory Roots
        static std::string DataDir()     { return s_projectRoot + "data/"; }
        static std::string TagsDir()     { return s_projectRoot + "tags/"; }
        static std::string MapsDir()     { return s_projectRoot + "maps/"; }
        static std::string ShadersDir() { return s_projectRoot + "shaders/"; }

        // Resolvers
        static std::string ResolveData(const std::string& subpath) {
            return DataDir() + CleanLeadingSlash(subpath);
        }

        static std::string ResolveTag(const std::string& subpath) {
            return TagsDir() + CleanLeadingSlash(subpath);
        }

        static std::string ResolveMap(const std::string& subpath) {
            return MapsDir() + CleanLeadingSlash(subpath);
        }

        static std::string ResolveShader(const std::string& subpath) {
            return ShadersDir() + CleanLeadingSlash(subpath);
        }

    private:
        inline static std::string s_projectRoot = "./";

        static std::string CleanLeadingSlash(const std::string& path) {
            if (!path.empty() && (path.front() == '/' || path.front() == '\\')) {
                return path.substr(1);
            }
            return path;
        }
    };
}