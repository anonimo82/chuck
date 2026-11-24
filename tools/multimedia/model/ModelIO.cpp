// Optional synchronous model I/O for ChucK.
#include "chuck_dl.h"
#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <filesystem>
#include <string>
#include <algorithm>
#include <cctype>

namespace {
namespace fs = std::filesystem;
std::string extension(const std::string &path) {
    auto ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c){ return std::tolower(c); });
    return ext;
}
bool supportedInput(const std::string &path) {
    const auto ext = extension(path);
    return ext == ".obj" || ext == ".fbx" || ext == ".gltf" ||
           ext == ".glb" || ext == ".dae" || ext == ".stl" ||
           ext == ".ply" || ext == ".3ds" || ext == ".off" ||
           ext == ".x3d" || ext == ".blend";
}
const aiScene *load(Assimp::Importer &importer, const std::string &path) {
    if(!supportedInput(path) || !fs::is_regular_file(fs::path(path))) return nullptr;
    const auto *scene = importer.ReadFile(path, aiProcess_ValidateDataStructure);
    return scene && scene->mRootNode && !(scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
        ? scene : nullptr;
}
std::string exportId(const std::string &path) {
    const auto ext = extension(path);
    if(ext == ".obj") return "obj";
    if(ext == ".fbx") return "fbx";
    if(ext == ".gltf") return "gltf2";
    if(ext == ".glb") return "glb2";
    if(ext == ".dae") return "collada";
    if(ext == ".stl") return "stl";
    if(ext == ".ply") return "ply";
    if(ext == ".3ds") return "3ds";
    if(ext == ".x3d") return "x3d";
    return "";
}
}
CK_DLL_SFUN(model_valid) {
    RETURN->v_int = 0;
    try {
        auto path = GET_NEXT_STRING_SAFE(ARGS);
        Assimp::Importer importer;
        RETURN->v_int = load(importer, path) ? 1 : 0;
    } catch(...) {}
}
CK_DLL_SFUN(model_meshes) {
    RETURN->v_int = -1;
    try {
        auto path = GET_NEXT_STRING_SAFE(ARGS);
        Assimp::Importer importer;
        const auto *scene = load(importer, path);
        if(scene) RETURN->v_int = scene->mNumMeshes;
    } catch(...) {}
}
CK_DLL_SFUN(model_vertices) {
    RETURN->v_int = -1;
    try {
        auto path = GET_NEXT_STRING_SAFE(ARGS);
        Assimp::Importer importer;
        const auto *scene = load(importer, path);
        if(!scene) return;
        t_CKINT count = 0;
        for(unsigned int i = 0; i < scene->mNumMeshes; ++i)
            count += scene->mMeshes[i]->mNumVertices;
        RETURN->v_int = count;
    } catch(...) {}
}
CK_DLL_SFUN(model_convert) {
    RETURN->v_int = 0;
    try {
        auto source = GET_NEXT_STRING_SAFE(ARGS);
        auto destination = GET_NEXT_STRING_SAFE(ARGS);
        const auto id = exportId(destination);
        if(id.empty()) return;
        auto target = fs::absolute(fs::path(destination));
        // Reserve a new output directory. Assimp exporters can write sidecars.
        // Require the caller to supply a path in a non-existing directory.
        auto dir = target.parent_path();
        if(!fs::is_directory(dir.parent_path()) || fs::exists(dir)) return;
        Assimp::Importer importer;
        const auto *scene = load(importer, source);
        if(!scene) return;
        Assimp::Exporter exporter;
        if(!fs::create_directory(dir)) return;
        if(exporter.Export(scene, id.c_str(), target.string().c_str()) == AI_SUCCESS
            && fs::is_regular_file(target)) RETURN->v_int = 1;
        // Preserve any partial output for inspection; never recursively delete.
    } catch(...) {}
}
CK_DLL_QUERY(ModelIO) {
    QUERY->setname(QUERY, "ModelIO");
    QUERY->begin_class(QUERY, "ModelIO", "Object");
    QUERY->add_sfun(QUERY, model_valid, "int", "valid");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->add_sfun(QUERY, model_meshes, "int", "meshes");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->add_sfun(QUERY, model_vertices, "int", "vertices");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->add_sfun(QUERY, model_convert, "int", "convert");
    QUERY->add_arg(QUERY, "string", "source");
    QUERY->add_arg(QUERY, "string", "destination");
    return QUERY->end_class(QUERY);
}
