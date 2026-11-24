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
#include <assimp/cexport.h>
#include <array>
#include <memory>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>

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

namespace {
struct ModelState {
    std::unique_ptr<Assimp::Importer> importer;
    const aiScene *scene=nullptr;
    std::array<double,9> controls{{0,0,0,0,0,0,1,1,1}};
    void reset() { controls={{0,0,0,0,0,0,1,1,1}}; }
    bool read(const std::string &path) {
        auto candidate=std::make_unique<Assimp::Importer>();
        const auto *next=load(*candidate,path);
        if(!next) return false;
        importer=std::move(candidate);
        scene=next;
        reset();
        return true;
    }
    bool save(const std::string &path) const {
        if(!scene) return false;
        auto id=exportId(path);
        if(id.empty()) return false;
        auto target=fs::absolute(fs::path(path));
        auto dir=target.parent_path();
        if(!fs::is_directory(dir.parent_path()) || fs::exists(dir)) return false;
        aiScene *copy=nullptr;
        aiCopyScene(scene,&copy);
        std::unique_ptr<aiScene,decltype(&aiFreeScene)> owned(copy,aiFreeScene);
        if(!copy || !copy->mRootNode) return false;
        aiMatrix4x4 t,rx,ry,rz,s;
        constexpr double rad=3.14159265358979323846/180.0;
        aiMatrix4x4::Translation(aiVector3D(controls[0],controls[1],controls[2]),t);
        aiMatrix4x4::RotationX(static_cast<ai_real>(std::remainder(controls[3],360.0)*rad),rx);
        aiMatrix4x4::RotationY(static_cast<ai_real>(std::remainder(controls[4],360.0)*rad),ry);
        aiMatrix4x4::RotationZ(static_cast<ai_real>(std::remainder(controls[5],360.0)*rad),rz);
        aiMatrix4x4::Scaling(aiVector3D(controls[6],controls[7],controls[8]),s);
        // A new parent preserves the original root's animation channels.
        auto parent=std::make_unique<aiNode>();
        std::string name="ChucK_Model3D";
        while(copy->mRootNode->FindNode(aiString(name))) {
            name+="_";
            if(name.size()>900) return false;
        }
        parent->mName=aiString(name);
        parent->mTransformation=t*rz*ry*rx*s;
        parent->mChildren=new aiNode*[1];
        parent->mChildren[0]=copy->mRootNode;
        parent->mNumChildren=1;
        copy->mRootNode->mParent=parent.get();
        copy->mRootNode=parent.release();
        Assimp::Exporter exporter;
        if(!fs::create_directory(dir)) return false;
        return exporter.Export(copy,id.c_str(),target.string().c_str())==AI_SUCCESS &&
               fs::is_regular_file(target);
    }
};
t_CKUINT modelOffset=0;
ModelState *modelState(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<ModelState *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,modelOffset)));
}
double modelGet(ModelState *p,size_t i) {
    return p?p->controls[i]:std::numeric_limits<double>::quiet_NaN();
}
double modelSet(ModelState *p,size_t i,double v) {
    if(!p || !std::isfinite(v) || (i<3 && std::abs(v)>1e9) ||
       (i>=3 && i<6 && std::abs(v)>1e9) ||
       (i>=6 && (v<0.0001 || v>10000))) return std::numeric_limits<double>::quiet_NaN();
    return p->controls[i]=v;
}
}
CK_DLL_CTOR(model3d_ctor) {
    static_assert(sizeof(t_CKUINT)>=sizeof(uintptr_t),"ChucK integer must hold native pointer");
    OBJ_MEMBER_UINT(SELF,modelOffset)=0;
    OBJ_MEMBER_UINT(SELF,modelOffset)=reinterpret_cast<uintptr_t>(new(std::nothrow) ModelState);
}
CK_DLL_DTOR(model3d_dtor) {
    delete modelState(SELF,API);
    OBJ_MEMBER_UINT(SELF,modelOffset)=0;
}
CK_DLL_MFUN(model3d_load) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API);
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        if(p && p->read(path)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_save) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API);
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        if(p && p->save(path)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_loaded) {
    auto *p=modelState(SELF,API);
    RETURN->v_int=p && p->scene;
}
CK_DLL_MFUN(model3d_meshes) {
    auto *p=modelState(SELF,API);
    RETURN->v_int=p && p->scene?static_cast<t_CKINT>(p->scene->mNumMeshes):-1;
}
CK_DLL_MFUN(model3d_vertices) {
    RETURN->v_int=-1;
    auto *p=modelState(SELF,API);
    if(!p || !p->scene) return;
    t_CKINT count=0;
    for(unsigned i=0;i<p->scene->mNumMeshes;++i) {
        auto n=p->scene->mMeshes[i]->mNumVertices;
        if(n>static_cast<uint64_t>(std::numeric_limits<t_CKINT>::max()-count)) return;
        count+=n;
    }
    RETURN->v_int=count;
}
CK_DLL_MFUN(model3d_reset) {
    if(auto *p=modelState(SELF,API)) p->reset();
}
CK_DLL_MFUN(model3d_clear) {
    if(auto *p=modelState(SELF,API)) { p->scene=nullptr; p->importer.reset(); p->reset(); }
}
CK_DLL_MFUN(model3d_get_x) { RETURN->v_float=modelGet(modelState(SELF,API),0); }
CK_DLL_MFUN(model3d_set_x) { RETURN->v_float=modelSet(modelState(SELF,API),0,GET_NEXT_FLOAT(ARGS)); }
CK_DLL_MFUN(model3d_get_y) { RETURN->v_float=modelGet(modelState(SELF,API),1); }
CK_DLL_MFUN(model3d_set_y) { RETURN->v_float=modelSet(modelState(SELF,API),1,GET_NEXT_FLOAT(ARGS)); }
CK_DLL_MFUN(model3d_get_z) { RETURN->v_float=modelGet(modelState(SELF,API),2); }
CK_DLL_MFUN(model3d_set_z) { RETURN->v_float=modelSet(modelState(SELF,API),2,GET_NEXT_FLOAT(ARGS)); }
CK_DLL_MFUN(model3d_get_rotationX) { RETURN->v_float=modelGet(modelState(SELF,API),3); }
CK_DLL_MFUN(model3d_set_rotationX) { RETURN->v_float=modelSet(modelState(SELF,API),3,GET_NEXT_FLOAT(ARGS)); }
CK_DLL_MFUN(model3d_get_rotationY) { RETURN->v_float=modelGet(modelState(SELF,API),4); }
CK_DLL_MFUN(model3d_set_rotationY) { RETURN->v_float=modelSet(modelState(SELF,API),4,GET_NEXT_FLOAT(ARGS)); }
CK_DLL_MFUN(model3d_get_rotationZ) { RETURN->v_float=modelGet(modelState(SELF,API),5); }
CK_DLL_MFUN(model3d_set_rotationZ) { RETURN->v_float=modelSet(modelState(SELF,API),5,GET_NEXT_FLOAT(ARGS)); }
CK_DLL_MFUN(model3d_get_scaleX) { RETURN->v_float=modelGet(modelState(SELF,API),6); }
CK_DLL_MFUN(model3d_set_scaleX) { RETURN->v_float=modelSet(modelState(SELF,API),6,GET_NEXT_FLOAT(ARGS)); }
CK_DLL_MFUN(model3d_get_scaleY) { RETURN->v_float=modelGet(modelState(SELF,API),7); }
CK_DLL_MFUN(model3d_set_scaleY) { RETURN->v_float=modelSet(modelState(SELF,API),7,GET_NEXT_FLOAT(ARGS)); }
CK_DLL_MFUN(model3d_get_scaleZ) { RETURN->v_float=modelGet(modelState(SELF,API),8); }
CK_DLL_MFUN(model3d_set_scaleZ) { RETURN->v_float=modelSet(modelState(SELF,API),8,GET_NEXT_FLOAT(ARGS)); }

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
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"Model3D","Object");
    modelOffset=QUERY->add_mvar(QUERY,"int","@modelNative",FALSE);
    if(modelOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,model3d_ctor);
    QUERY->add_dtor(QUERY,model3d_dtor);
    QUERY->add_mfun(QUERY,model3d_load,"int","load");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_mfun(QUERY,model3d_save,"int","save");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_mfun(QUERY,model3d_loaded,"int","loaded");
    QUERY->add_mfun(QUERY,model3d_meshes,"int","meshes");
    QUERY->add_mfun(QUERY,model3d_vertices,"int","vertices");
    QUERY->add_mfun(QUERY,model3d_reset,"void","reset");
    QUERY->add_mfun(QUERY,model3d_clear,"void","clear");
    QUERY->add_mfun(QUERY,model3d_get_x,"float","x");
    QUERY->add_mfun(QUERY,model3d_set_x,"float","x");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,model3d_get_y,"float","y");
    QUERY->add_mfun(QUERY,model3d_set_y,"float","y");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,model3d_get_z,"float","z");
    QUERY->add_mfun(QUERY,model3d_set_z,"float","z");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,model3d_get_rotationX,"float","rotationX");
    QUERY->add_mfun(QUERY,model3d_set_rotationX,"float","rotationX");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,model3d_get_rotationY,"float","rotationY");
    QUERY->add_mfun(QUERY,model3d_set_rotationY,"float","rotationY");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,model3d_get_rotationZ,"float","rotationZ");
    QUERY->add_mfun(QUERY,model3d_set_rotationZ,"float","rotationZ");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,model3d_get_scaleX,"float","scaleX");
    QUERY->add_mfun(QUERY,model3d_set_scaleX,"float","scaleX");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,model3d_get_scaleY,"float","scaleY");
    QUERY->add_mfun(QUERY,model3d_set_scaleY,"float","scaleY");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,model3d_get_scaleZ,"float","scaleZ");
    QUERY->add_mfun(QUERY,model3d_set_scaleZ,"float","scaleZ");
    QUERY->add_arg(QUERY,"float","value");
    return QUERY->end_class(QUERY);
}
