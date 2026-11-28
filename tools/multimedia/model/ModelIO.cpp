// Optional synchronous model I/O for ChucK.
#include "chuck_dl.h"
#include "../file_compat.h"
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
#include <vector>
#include <utility>
#include <unordered_map>
#include <assimp/material.h>

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
           ext == ".x3d";
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
    std::unique_ptr<aiScene> generated;
    const aiScene *scene=nullptr;
    fs::path sourceDirectory;
    struct Playback {
        t_CKINT clip=-1, target=-1, loop=0, paused=0, ended=0;
        double cursor=0, targetCursor=0, speed=1, fade=0, elapsed=0;
    } playback;
    std::unordered_map<const aiNode*,aiMatrix4x4> importedPose;
    std::array<double,9> controls{{0,0,0,0,0,0,1,1,1}};
    void reset() { controls={{0,0,0,0,0,0,1,1,1}}; playback=Playback{}; }
    bool read(const std::string &path) {
        auto candidate=std::make_unique<Assimp::Importer>();
        const auto *next=load(*candidate,path);
        if(!next) return false;
        auto directory=fs::canonical(fs::path(path)).parent_path();
        importedPose.clear();
        sourceDirectory.swap(directory);
        generated.reset();
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
    if(auto *p=modelState(SELF,API)) { p->importedPose.clear(); p->sourceDirectory.clear(); p->scene=nullptr; p->importer.reset(); p->generated.reset(); p->reset(); }
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


namespace {
bool coordinate(double v) { return std::isfinite(v) && std::abs(v)<=1e9; }
bool makeMesh(ModelState *p,const std::vector<aiVector3D> &vertices,
              const std::vector<unsigned> &indices) {
    if(!p || vertices.size()<3 || vertices.size()>1000000 ||
       indices.size()<3 || indices.size()>3000000 || indices.size()%3) return false;
    for(size_t i=0;i<indices.size();i+=3) {
        unsigned a=indices[i],b=indices[i+1],c=indices[i+2];
        if(a>=vertices.size() || b>=vertices.size() || c>=vertices.size() ||
           a==b || a==c || b==c) return false;
    }
    auto scene=std::make_unique<aiScene>();
    scene->mRootNode=new aiNode();
    scene->mRootNode->mName=aiString("ProceduralMesh");
    scene->mMeshes=new aiMesh*[1]{nullptr}; scene->mNumMeshes=1;
    auto *mesh=scene->mMeshes[0]=new aiMesh();
    mesh->mName=aiString("Mesh");
    mesh->mPrimitiveTypes=aiPrimitiveType_TRIANGLE;
    mesh->mVertices=new aiVector3D[vertices.size()];
    mesh->mNumVertices=static_cast<unsigned>(vertices.size());
    std::copy(vertices.begin(),vertices.end(),mesh->mVertices);
    mesh->mFaces=new aiFace[indices.size()/3];
    mesh->mNumFaces=static_cast<unsigned>(indices.size()/3);
    for(unsigned i=0;i<mesh->mNumFaces;++i) {
        auto &face=mesh->mFaces[i];
        face.mIndices=new unsigned[3]{indices[i*3],indices[i*3+1],indices[i*3+2]};
        face.mNumIndices=3;
    }
    scene->mMaterials=new aiMaterial*[1]{nullptr}; scene->mNumMaterials=1;
    scene->mMaterials[0]=new aiMaterial();
    aiString name("DefaultMaterial");
    if(scene->mMaterials[0]->AddProperty(&name,AI_MATKEY_NAME)!=AI_SUCCESS) return false;
    mesh->mMaterialIndex=0;
    scene->mRootNode->mMeshes=new unsigned[1]{0};
    scene->mRootNode->mNumMeshes=1;
    p->importedPose.clear();
    p->importer.reset();
    p->sourceDirectory.clear();
    p->generated=std::move(scene);
    p->scene=p->generated.get(); p->reset();
    return true;
}
const aiMesh *meshAt(ModelState *p,t_CKINT mesh) {
    if(!p || !p->scene || mesh<0 || static_cast<uint64_t>(mesh)>=p->scene->mNumMeshes) return nullptr;
    return p->scene->mMeshes[mesh];
}
}
CK_DLL_MFUN(model3d_create_mesh) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API);
        auto xyz=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto tris=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!p || !xyz || !tris) return;
        auto n=API->object->array_float_size(xyz),m=API->object->array_int_size(tris);
        if(n<9 || n>3000000 || n%3 || m<3 || m>3000000 || m%3) return;
        std::vector<aiVector3D> vertices;
        vertices.reserve(n/3);
        for(t_CKINT i=0;i<n;i+=3) {
            double x=API->object->array_float_get_idx(xyz,i),y=API->object->array_float_get_idx(xyz,i+1),
                   z=API->object->array_float_get_idx(xyz,i+2);
            if(!coordinate(x)||!coordinate(y)||!coordinate(z)) return;
            vertices.emplace_back(x,y,z);
        }
        std::vector<unsigned> indices; indices.reserve(m);
        for(t_CKINT i=0;i<m;++i) {
            auto v=API->object->array_int_get_idx(tris,i);
            if(v<0 || v>=n/3) return;
            indices.push_back(static_cast<unsigned>(v));
        }
        RETURN->v_int=makeMesh(p,vertices,indices);
    } catch(...) {}
}
CK_DLL_MFUN(model3d_create_box) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API);
        auto w=GET_NEXT_FLOAT(ARGS),h=GET_NEXT_FLOAT(ARGS),d=GET_NEXT_FLOAT(ARGS);
        if(!p || !coordinate(w)||!coordinate(h)||!coordinate(d)||w<0.0001||h<0.0001||d<0.0001) return;
        ai_real x=w/2,y=h/2,z=d/2;
        std::vector<aiVector3D> vertices{{-x,-y,-z},{x,-y,-z},{x,y,-z},{-x,y,-z},
                                        {-x,-y,z},{x,-y,z},{x,y,z},{-x,y,z}};
        std::vector<unsigned> faces{0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,
                                    3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5};
        RETURN->v_int=makeMesh(p,vertices,faces);
    } catch(...) {}
}
CK_DLL_MFUN(model3d_mesh_vertices) {
    auto *m=meshAt(modelState(SELF,API),GET_NEXT_INT(ARGS));
    RETURN->v_int=m?static_cast<t_CKINT>(m->mNumVertices):-1;
}
CK_DLL_MFUN(model3d_faces) {
    auto *m=meshAt(modelState(SELF,API),GET_NEXT_INT(ARGS));
    RETURN->v_int=m?static_cast<t_CKINT>(m->mNumFaces):-1;
}
CK_DLL_MFUN(model3d_vertex) {
    auto *p=modelState(SELF,API);
    auto mesh=GET_NEXT_INT(ARGS),index=GET_NEXT_INT(ARGS),axis=GET_NEXT_INT(ARGS);
    auto *m=meshAt(p,mesh);
    RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(!m || index<0 || static_cast<uint64_t>(index)>=m->mNumVertices || axis<0 || axis>2) return;
    const auto &v=m->mVertices[index];
    RETURN->v_float=axis==0?v.x:axis==1?v.y:v.z;
}
CK_DLL_MFUN(model3d_face_size) {
    auto *p=modelState(SELF,API);
    auto mesh=GET_NEXT_INT(ARGS),face=GET_NEXT_INT(ARGS);
    auto *m=meshAt(p,mesh);
    RETURN->v_int=-1;
    if(m && face>=0 && static_cast<uint64_t>(face)<m->mNumFaces)
        RETURN->v_int=m->mFaces[face].mNumIndices;
}
CK_DLL_MFUN(model3d_face_index) {
    auto *p=modelState(SELF,API);
    auto mesh=GET_NEXT_INT(ARGS),face=GET_NEXT_INT(ARGS),index=GET_NEXT_INT(ARGS);
    auto *m=meshAt(p,mesh);
    RETURN->v_int=-1;
    if(m && face>=0 && static_cast<uint64_t>(face)<m->mNumFaces &&
       index>=0 && static_cast<uint64_t>(index)<m->mFaces[face].mNumIndices)
        RETURN->v_int=m->mFaces[face].mIndices[index];
}
CK_DLL_MFUN(model3d_set_vertex) {
    RETURN->v_int=0;
    auto *p=modelState(SELF,API);
    auto index=GET_NEXT_INT(ARGS);
    auto x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS),z=GET_NEXT_FLOAT(ARGS);
    if(!p || !p->generated || index<0 || static_cast<uint64_t>(index)>=p->generated->mMeshes[0]->mNumVertices ||
       !coordinate(x)||!coordinate(y)||!coordinate(z)) return;
    auto *mesh=p->generated->mMeshes[0];
    mesh->mVertices[index]=aiVector3D(x,y,z);
    delete[] mesh->mNormals; mesh->mNormals=nullptr;
    delete[] mesh->mTangents; mesh->mTangents=nullptr;
    delete[] mesh->mBitangents; mesh->mBitangents=nullptr;
    RETURN->v_int=1;
}
CK_DLL_MFUN(model3d_export_vertices) {
    RETURN->v_int=0;
    auto *p=modelState(SELF,API); auto index=GET_NEXT_INT(ARGS);
    auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
    auto *m=meshAt(p,index);
    if(!m || !a || API->object->array_float_size(a)!=static_cast<int64_t>(m->mNumVertices)*3) return;
    for(unsigned i=0;i<m->mNumVertices;++i) {
        auto &v=m->mVertices[i];
        API->object->array_float_set_idx(a,static_cast<int64_t>(i)*3,v.x);
        API->object->array_float_set_idx(a,static_cast<int64_t>(i)*3+1,v.y);
        API->object->array_float_set_idx(a,static_cast<int64_t>(i)*3+2,v.z);
    }
    RETURN->v_int=1;
}
CK_DLL_MFUN(model3d_export_triangles) {
    RETURN->v_int=0;
    auto *p=modelState(SELF,API); auto index=GET_NEXT_INT(ARGS);
    auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
    auto *m=meshAt(p,index);
    if(!m || !a || API->object->array_int_size(a)!=static_cast<int64_t>(m->mNumFaces)*3) return;
    for(unsigned i=0;i<m->mNumFaces;++i) if(m->mFaces[i].mNumIndices!=3) return;
    for(unsigned i=0;i<m->mNumFaces;++i)
        for(int j=0;j<3;++j) API->object->array_int_set_idx(a,static_cast<int64_t>(i)*3+j,m->mFaces[i].mIndices[j]);
    RETURN->v_int=1;
}


namespace {
aiMesh *editableMesh(ModelState *p,t_CKINT index) {
    return p && p->generated && index>=0 && static_cast<uint64_t>(index)<p->generated->mNumMeshes
        ?p->generated->mMeshes[index]:nullptr;
}
void invalidateNormals(aiMesh *m) {
    delete[] m->mNormals; m->mNormals=nullptr;
    delete[] m->mTangents; m->mTangents=nullptr;
    delete[] m->mBitangents; m->mBitangents=nullptr;
}
}
CK_DLL_MFUN(model3d_add_mesh) {
    RETURN->v_int=-1;
    try {
        auto *p=modelState(SELF,API);
        auto xyz=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto tris=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!p||!p->generated||!xyz||!tris||p->generated->mNumMeshes>=256) return;
        auto n=API->object->array_float_size(xyz),k=API->object->array_int_size(tris);
        if(n<9||n>3000000||n%3||k<3||k>3000000||k%3) return;
        auto *scene=p->generated.get();
        uint64_t nv=n/3,nf=k/3;
        for(unsigned i=0;i<scene->mNumMeshes;++i) {
            nv+=scene->mMeshes[i]->mNumVertices; nf+=scene->mMeshes[i]->mNumFaces;
        }
        if(nv>1000000||nf>1000000) return;
        std::vector<aiVector3D> vertices; std::vector<unsigned> indices;
        vertices.reserve(n/3); indices.reserve(k);
        for(t_CKINT i=0;i<n;i+=3) {
            double x=API->object->array_float_get_idx(xyz,i),y=API->object->array_float_get_idx(xyz,i+1),
                   z=API->object->array_float_get_idx(xyz,i+2);
            if(!coordinate(x)||!coordinate(y)||!coordinate(z)) return;
            vertices.emplace_back(x,y,z);
        }
        for(t_CKINT i=0;i<k;++i) {
            auto v=API->object->array_int_get_idx(tris,i);
            if(v<0||v>=n/3) return;
            indices.push_back(static_cast<unsigned>(v));
        }
        ModelState candidate;
        if(!makeMesh(&candidate,vertices,indices)) return;
        unsigned count=scene->mNumMeshes;
        auto meshes=std::make_unique<aiMesh*[]>(count+1);
        auto refs=std::make_unique<unsigned[]>(count+1);
        for(unsigned i=0;i<count;++i) { meshes[i]=scene->mMeshes[i]; refs[i]=i; }
        auto *added=candidate.generated->mMeshes[0];
        added->mName=aiString(std::string("Mesh_")+std::to_string(count));
        meshes[count]=added; refs[count]=count;
        candidate.generated->mMeshes[0]=nullptr;
        delete[] scene->mMeshes; delete[] scene->mRootNode->mMeshes;
        scene->mMeshes=meshes.release(); scene->mNumMeshes=count+1;
        scene->mRootNode->mMeshes=refs.release(); scene->mRootNode->mNumMeshes=count+1;
        RETURN->v_int=count;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_set_mesh_vertex) {
    RETURN->v_int=0;
    auto *p=modelState(SELF,API);
    auto mesh=GET_NEXT_INT(ARGS),index=GET_NEXT_INT(ARGS);
    auto x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS),z=GET_NEXT_FLOAT(ARGS);
    auto *m=editableMesh(p,mesh);
    if(!m||index<0||static_cast<uint64_t>(index)>=m->mNumVertices||
       !coordinate(x)||!coordinate(y)||!coordinate(z)) return;
    m->mVertices[index]=aiVector3D(x,y,z); invalidateNormals(m);
    RETURN->v_int=1;
}
CK_DLL_MFUN(model3d_transform_mesh) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API); auto mesh=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto *m=editableMesh(p,mesh);
        if(!m||!a||API->object->array_float_size(a)!=12) return;
        double v[12];
        for(int i=0;i<12;++i) { v[i]=API->object->array_float_get_idx(a,i); if(!coordinate(v[i])) return; }
        std::vector<aiVector3D> next(m->mNumVertices);
        for(unsigned i=0;i<m->mNumVertices;++i) {
            auto q=m->mVertices[i];
            double x=v[0]*q.x+v[1]*q.y+v[2]*q.z+v[3],
                   y=v[4]*q.x+v[5]*q.y+v[6]*q.z+v[7],
                   z=v[8]*q.x+v[9]*q.y+v[10]*q.z+v[11];
            if(!coordinate(x)||!coordinate(y)||!coordinate(z)) return;
            next[i]=aiVector3D(x,y,z);
        }
        std::copy(next.begin(),next.end(),m->mVertices);
        invalidateNormals(m); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_compute_normals) {
    RETURN->v_int=0;
    try {
        auto *m=editableMesh(modelState(SELF,API),GET_NEXT_INT(ARGS));
        if(!m) return;
        struct V { double x=0,y=0,z=0; };
        std::vector<V> sums(m->mNumVertices);
        for(unsigned i=0;i<m->mNumFaces;++i) {
            auto &f=m->mFaces[i]; if(f.mNumIndices!=3) return;
            unsigned a=f.mIndices[0],b=f.mIndices[1],c=f.mIndices[2];
            if(a>=m->mNumVertices||b>=m->mNumVertices||c>=m->mNumVertices) return;
            auto A=m->mVertices[a],B=m->mVertices[b],C=m->mVertices[c];
            double ux=double(B.x)-A.x,uy=double(B.y)-A.y,uz=double(B.z)-A.z;
            double vx=double(C.x)-A.x,vy=double(C.y)-A.y,vz=double(C.z)-A.z;
            V cross{uy*vz-uz*vy,uz*vx-ux*vz,ux*vy-uy*vx};
            for(unsigned j:{a,b,c}) { sums[j].x+=cross.x; sums[j].y+=cross.y; sums[j].z+=cross.z; }
        }
        auto next=std::make_unique<aiVector3D[]>(m->mNumVertices);
        for(unsigned i=0;i<m->mNumVertices;++i) {
            auto v=sums[i]; double len=std::hypot(v.x,v.y,v.z);
            if(!std::isfinite(len)||len<=0) return;
            next[i]=aiVector3D(v.x/len,v.y/len,v.z/len);
        }
        invalidateNormals(m); m->mNormals=next.release(); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_has_normals) {
    auto *m=meshAt(modelState(SELF,API),GET_NEXT_INT(ARGS));
    RETURN->v_int=m && m->HasNormals();
}
CK_DLL_MFUN(model3d_export_normals) {
    RETURN->v_int=0;
    auto *p=modelState(SELF,API); auto index=GET_NEXT_INT(ARGS);
    auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
    auto *m=meshAt(p,index);
    if(!m||!m->HasNormals()||!a||API->object->array_float_size(a)!=static_cast<int64_t>(m->mNumVertices)*3) return;
    for(unsigned i=0;i<m->mNumVertices;++i) {
        auto v=m->mNormals[i];
        API->object->array_float_set_idx(a,3*i,v.x);
        API->object->array_float_set_idx(a,3*i+1,v.y);
        API->object->array_float_set_idx(a,3*i+2,v.z);
    }
    RETURN->v_int=1;
}
CK_DLL_MFUN(model3d_set_uv) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API); auto index=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto *m=editableMesh(p,index);
        if(!m||!a||API->object->array_float_size(a)!=static_cast<int64_t>(m->mNumVertices)*2) return;
        auto next=std::make_unique<aiVector3D[]>(m->mNumVertices);
        for(unsigned i=0;i<m->mNumVertices;++i) {
            double u=API->object->array_float_get_idx(a,2*i),v=API->object->array_float_get_idx(a,2*i+1);
            if(!coordinate(u)||!coordinate(v)) return;
            next[i]=aiVector3D(u,v,0);
        }
        delete[] m->mTextureCoords[0]; m->mTextureCoords[0]=next.release(); m->mNumUVComponents[0]=2;
        delete[] m->mTangents; m->mTangents=nullptr;
        delete[] m->mBitangents; m->mBitangents=nullptr;
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_export_uv) {
    RETURN->v_int=0;
    auto *p=modelState(SELF,API); auto index=GET_NEXT_INT(ARGS);
    auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
    auto *m=meshAt(p,index);
    if(!m||!m->HasTextureCoords(0)||m->mNumUVComponents[0]!=2||!a||
       API->object->array_float_size(a)!=static_cast<int64_t>(m->mNumVertices)*2) return;
    for(unsigned i=0;i<m->mNumVertices;++i) {
        API->object->array_float_set_idx(a,2*i,m->mTextureCoords[0][i].x);
        API->object->array_float_set_idx(a,2*i+1,m->mTextureCoords[0][i].y);
    }
    RETURN->v_int=1;
}


namespace {
bool unit(double v) { return std::isfinite(v)&&v>=0&&v<=1; }
}
CK_DLL_MFUN(model3d_material) {
    RETURN->v_int=-1;
    try {
        auto *p=modelState(SELF,API);
        auto name=GET_NEXT_STRING_SAFE(ARGS);
        auto r=GET_NEXT_FLOAT(ARGS),g=GET_NEXT_FLOAT(ARGS),b=GET_NEXT_FLOAT(ARGS),a=GET_NEXT_FLOAT(ARGS);
        if(!p||!p->generated||name.empty()||name.size()>255||!unit(r)||!unit(g)||!unit(b)||!unit(a)) return;
        auto *scene=p->generated.get(); unsigned n=scene->mNumMaterials;
        if(n>=256) return;
        auto material=std::make_unique<aiMaterial>();
        aiString label(name); aiColor4D color(r,g,b,a); ai_real opacity=a;
        if(material->AddProperty(&label,AI_MATKEY_NAME)!=AI_SUCCESS||
           material->AddProperty(&color,1,AI_MATKEY_COLOR_DIFFUSE)!=AI_SUCCESS||
           material->AddProperty(&opacity,1,AI_MATKEY_OPACITY)!=AI_SUCCESS) return;
        auto list=std::make_unique<aiMaterial*[]>(n+1);
        for(unsigned i=0;i<n;++i) list[i]=scene->mMaterials[i];
        list[n]=material.release();
        delete[] scene->mMaterials; scene->mMaterials=list.release(); scene->mNumMaterials=n+1;
        RETURN->v_int=n;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_material_count) {
    auto *p=modelState(SELF,API);
    RETURN->v_int=p&&p->scene?static_cast<t_CKINT>(p->scene->mNumMaterials):-1;
}
CK_DLL_MFUN(model3d_material_at) {
    auto *m=meshAt(modelState(SELF,API),GET_NEXT_INT(ARGS));
    RETURN->v_int=m?static_cast<t_CKINT>(m->mMaterialIndex):-1;
}
namespace {
const aiMaterial *materialByIndex(ModelState *p,t_CKINT index) {
    if(!p||!p->scene||index<0||static_cast<uint64_t>(index)>=p->scene->mNumMaterials) return nullptr;
    return p->scene->mMaterials[index];
}
}
CK_DLL_MFUN(model3d_export_material) {
    RETURN->v_int=0;
    try {
        auto index=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto *m=materialByIndex(modelState(SELF,API),index);
        if(!m||!a||API->object->array_float_size(a)!=6) return;
        aiColor4D color(1,1,1,1);
        if(m->Get(AI_MATKEY_BASE_COLOR,color)!=AI_SUCCESS)
            if(m->Get(AI_MATKEY_COLOR_DIFFUSE,color)!=AI_SUCCESS) color=aiColor4D(1,1,1,1);
        ai_real opacity=color.a,metallic=0,roughness=1;
        // Assimp opacity may duplicate base-color alpha; do not multiply them.
        if(m->Get(AI_MATKEY_OPACITY,opacity)!=AI_SUCCESS) opacity=color.a;
        if(m->Get(AI_MATKEY_METALLIC_FACTOR,metallic)!=AI_SUCCESS) metallic=0;
        if(m->Get(AI_MATKEY_ROUGHNESS_FACTOR,roughness)!=AI_SUCCESS) roughness=1;
        double values[]={color.r,color.g,color.b,opacity,metallic,roughness};
        for(double v:values) if(!std::isfinite(v)) return;
        for(unsigned i=0;i<6;++i)
            API->object->array_float_set_idx(a,i,std::clamp(values[i],0.0,1.0));
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_export_material_lighting) {
    RETURN->v_int=0;
    try {
        auto index=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto *m=materialByIndex(modelState(SELF,API),index);
        if(!m||!a||API->object->array_float_size(a)!=8) return;
        aiColor3D emission(0,0,0),specular(0.2f,0.2f,0.2f);
        ai_real shine=64;
        int twoSided=0;
        if(m->Get(AI_MATKEY_COLOR_EMISSIVE,emission)!=AI_SUCCESS) emission=aiColor3D(0,0,0);
        if(m->Get(AI_MATKEY_COLOR_SPECULAR,specular)!=AI_SUCCESS) specular=aiColor3D(0.2f,0.2f,0.2f);
        if(m->Get(AI_MATKEY_SHININESS,shine)!=AI_SUCCESS) shine=64;
        if(m->Get(AI_MATKEY_TWOSIDED,twoSided)!=AI_SUCCESS) twoSided=0;
        double values[]={emission.r,emission.g,emission.b,specular.r,specular.g,specular.b,shine,twoSided?1.0:0.0};
        for(double v:values) if(!std::isfinite(v)) return;
        for(unsigned i=0;i<6;++i) values[i]=std::clamp(values[i],0.0,1.0);
        values[6]=std::clamp(values[6],0.0,10000.0);
        for(unsigned i=0;i<8;++i) API->object->array_float_set_idx(a,i,values[i]);
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_base_texture_count) {
    RETURN->v_int=-1;
    auto *m=materialByIndex(modelState(SELF,API),GET_NEXT_INT(ARGS));
    if(!m) return;
    auto n=m->GetTextureCount(aiTextureType_BASE_COLOR);
    RETURN->v_int=n?n:m->GetTextureCount(aiTextureType_DIFFUSE);
}
namespace {
// Resolve only local relative paths beneath the model's canonical directory.
// Embedded resources, absolute paths, URLs and traversal are not accepted.
std::string externalBaseTexture(ModelState *p,t_CKINT index,t_CKINT slot) {
    auto *m=materialByIndex(p,index);
    if(!m||p->sourceDirectory.empty()||slot<0) return {};
    auto kind=m->GetTextureCount(aiTextureType_BASE_COLOR)?aiTextureType_BASE_COLOR:aiTextureType_DIFFUSE;
    if(static_cast<uint64_t>(slot)>=m->GetTextureCount(kind)) return {};
    aiString value;
    unsigned uvIndex=0;
    aiTextureMapping mapping=aiTextureMapping_UV;
    if(m->GetTexture(kind,static_cast<unsigned>(slot),&value,&mapping,&uvIndex)!=AI_SUCCESS||
       mapping!=aiTextureMapping_UV||uvIndex!=0) return {};
    std::string name(value.C_Str());
    if(name.empty()||name[0]=='*'||name.find(':')!=std::string::npos) return {};
    std::replace(name.begin(),name.end(),'\\','/');
    fs::path relative(name);
    if(relative.is_absolute()||relative.has_root_path()) return {};
    for(const auto &part:relative) if(part=="..") return {};
    auto candidate=fs::canonical(p->sourceDirectory/relative);
    auto root=p->sourceDirectory;
    auto a=root.begin(),b=candidate.begin();
    for(;a!=root.end();++a,++b)
        if(b==candidate.end()||*a!=*b) return {};
    if(!fs::is_regular_file(candidate)) return {};
    return candidate.string();
}
}
CK_DLL_MFUN(model3d_base_texture_path) {
    std::string result;
    try {
        auto index=GET_NEXT_INT(ARGS),slot=GET_NEXT_INT(ARGS);
        result=externalBaseTexture(modelState(SELF,API),index,slot);
    } catch(...) {}
    RETURN->v_string=API->object->create_string(VM,result.c_str(),FALSE);
}
namespace {
const aiTexture *embeddedBaseTexture(ModelState *p,t_CKINT index,t_CKINT slot) {
    auto *m=materialByIndex(p,index);
    if(!m||slot<0) return nullptr;
    auto kind=m->GetTextureCount(aiTextureType_BASE_COLOR)?aiTextureType_BASE_COLOR:aiTextureType_DIFFUSE;
    if(static_cast<uint64_t>(slot)>=m->GetTextureCount(kind)) return nullptr;
    aiString value;
    unsigned uvIndex=0;
    aiTextureMapping mapping=aiTextureMapping_UV;
    if(m->GetTexture(kind,static_cast<unsigned>(slot),&value,&mapping,&uvIndex)!=AI_SUCCESS||
       mapping!=aiTextureMapping_UV||uvIndex!=0) return nullptr;
    return p->scene->GetEmbeddedTexture(value.C_Str());
}
// Compressed payloads are copied verbatim; raw texels are encoded as PPM
// only when opaque (PPM cannot preserve alpha).
std::string embeddedFormat(const aiTexture *tex) {
    if(!tex||!tex->pcData||!tex->mWidth) return {};
    if(tex->mHeight) {
        uint64_t count=uint64_t(tex->mWidth)*tex->mHeight;
        if(count>16000000) return {};
        for(uint64_t i=0;i<count;++i) if(tex->pcData[i].a!=255) return {};
        return "ppm";
    }
    if(tex->mWidth>64*1024*1024) return {};
    const auto *bytes=reinterpret_cast<const unsigned char*>(tex->pcData);
    if(tex->mWidth>=8&&bytes[0]==137&&bytes[1]==80&&bytes[2]==78&&bytes[3]==71&&
       bytes[4]==13&&bytes[5]==10&&bytes[6]==26&&bytes[7]==10) return "png";
    if(tex->mWidth>=3&&bytes[0]==255&&bytes[1]==216&&bytes[2]==255) return "jpg";
    return {};
}
bool writeEmbedded(const aiTexture *tex,const std::string &path) {
    auto format=embeddedFormat(tex);
    if(format.empty()) return false;
    auto ext=extension(path);
    if(ext!="."+format&&!(format=="jpg"&&ext==".jpeg")) return false;
    // Prepare allocations before reserving the destination.
    std::vector<unsigned char> rgb;
    std::string header;
    if(tex->mHeight) {
        auto count=uint64_t(tex->mWidth)*tex->mHeight;
        rgb.resize(count*3);
        for(uint64_t i=0;i<count;++i) {
            rgb[3*i]=tex->pcData[i].r; rgb[3*i+1]=tex->pcData[i].g; rgb[3*i+2]=tex->pcData[i].b;
        }
        header="P6\n"+std::to_string(tex->mWidth)+" "+std::to_string(tex->mHeight)+"\n255\n";
    }
    FILE *file=mediaio::openNew(path.c_str());
    if(!file) return false;
    bool ok;
    if(tex->mHeight) {
        ok=std::fwrite(header.data(),1,header.size(),file)==header.size()&&
           std::fwrite(rgb.data(),1,rgb.size(),file)==rgb.size();
    } else {
        ok=std::fwrite(tex->pcData,1,tex->mWidth,file)==tex->mWidth;
    }
    if(std::fclose(file)!=0) ok=false;
    if(!ok) std::remove(path.c_str());
    return ok;
}
}
CK_DLL_MFUN(model3d_embedded_texture_format) {
    std::string result;
    try {
        auto index=GET_NEXT_INT(ARGS),slot=GET_NEXT_INT(ARGS);
        result=embeddedFormat(embeddedBaseTexture(modelState(SELF,API),index,slot));
    } catch(...) {}
    RETURN->v_string=API->object->create_string(VM,result.c_str(),FALSE);
}
CK_DLL_MFUN(model3d_export_embedded_texture) {
    RETURN->v_int=0;
    try {
        auto index=GET_NEXT_INT(ARGS),slot=GET_NEXT_INT(ARGS);
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        if(path.empty()||path.find('\0')!=std::string::npos) return;
        RETURN->v_int=writeEmbedded(embeddedBaseTexture(modelState(SELF,API),index,slot),path);
    } catch(...) {}
}
CK_DLL_MFUN(model3d_assign_material) {
    RETURN->v_int=0;
    auto *p=modelState(SELF,API); auto mesh=GET_NEXT_INT(ARGS),material=GET_NEXT_INT(ARGS);
    auto *m=editableMesh(p,mesh);
    if(!m||material<0||static_cast<uint64_t>(material)>=p->generated->mNumMaterials) return;
    m->mMaterialIndex=material; RETURN->v_int=1;
}
CK_DLL_MFUN(model3d_set_colors) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API); auto mesh=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto *m=editableMesh(p,mesh);
        if(!m||!a||API->object->array_float_size(a)!=static_cast<int64_t>(m->mNumVertices)*4) return;
        auto next=std::make_unique<aiColor4D[]>(m->mNumVertices);
        for(unsigned i=0;i<m->mNumVertices;++i) {
            double v[4];
            for(int j=0;j<4;++j) { v[j]=API->object->array_float_get_idx(a,4*i+j); if(!unit(v[j])) return; }
            next[i]=aiColor4D(v[0],v[1],v[2],v[3]);
        }
        delete[] m->mColors[0]; m->mColors[0]=next.release(); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_export_colors) {
    RETURN->v_int=0;
    auto *p=modelState(SELF,API); auto mesh=GET_NEXT_INT(ARGS);
    auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
    auto *m=meshAt(p,mesh);
    if(!m||!m->HasVertexColors(0)||!a||API->object->array_float_size(a)!=static_cast<int64_t>(m->mNumVertices)*4) return;
    for(unsigned i=0;i<m->mNumVertices;++i) {
        auto c=m->mColors[0][i];
        API->object->array_float_set_idx(a,4*i,c.r);
        API->object->array_float_set_idx(a,4*i+1,c.g);
        API->object->array_float_set_idx(a,4*i+2,c.b);
        API->object->array_float_set_idx(a,4*i+3,c.a);
    }
    RETURN->v_int=1;
}
// Minimal flat bind-pose skeleton: identity child nodes, identity inverse bind matrices.
// One assignment per mesh, dense vertex-major input, at most four nonzero influences.
CK_DLL_MFUN(model3d_set_skin) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API); auto mesh=GET_NEXT_INT(ARGS),count=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto *m=editableMesh(p,mesh);
        if(!m||m->HasBones()||count<1||count>64||!a||
           static_cast<uint64_t>(m->mNumVertices)*count>16000000||
           API->object->array_float_size(a)!=static_cast<int64_t>(m->mNumVertices)*count) return;
        std::vector<std::vector<aiVertexWeight>> weights(count);
        for(unsigned i=0;i<m->mNumVertices;++i) {
            double sum=0,values[64]; int nonzero=0;
            for(int j=0;j<count;++j) {
                double v=API->object->array_float_get_idx(a,static_cast<int64_t>(i)*count+j);
                if(!unit(v)) return;
                values[j]=v; sum+=v; if(v>0) ++nonzero;
            }
            if(sum<=0||nonzero>4) return;
            for(int j=0;j<count;++j) if(values[j]>0)
                weights[j].emplace_back(i,static_cast<ai_real>(values[j]/sum));
        }
        auto *root=p->generated->mRootNode;
        std::vector<std::unique_ptr<aiBone>> bones;
        std::vector<std::unique_ptr<aiNode>> nodes;
        for(int j=0;j<count;++j) {
            if(weights[j].empty()) return;
            aiString name("Skin_"+std::to_string(mesh)+"_"+std::to_string(j));
            if(root->FindNode(name)) return;
            auto bone=std::make_unique<aiBone>(); bone->mName=name;
            bone->mWeights=new aiVertexWeight[weights[j].size()];
            bone->mNumWeights=static_cast<unsigned>(weights[j].size());
            std::copy(weights[j].begin(),weights[j].end(),bone->mWeights);
            auto node=std::make_unique<aiNode>(); node->mName=name; node->mParent=root;
            bones.push_back(std::move(bone)); nodes.push_back(std::move(node));
        }
        auto boneList=std::make_unique<aiBone*[]>(count);
        auto childList=std::make_unique<aiNode*[]>(root->mNumChildren+count);
        for(unsigned j=0;j<root->mNumChildren;++j) childList[j]=root->mChildren[j];
        for(int j=0;j<count;++j) {
            boneList[j]=bones[j].release(); childList[root->mNumChildren+j]=nodes[j].release();
        }
        delete[] root->mChildren; root->mChildren=childList.release(); root->mNumChildren+=count;
        m->mBones=boneList.release(); m->mNumBones=count;
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_bones) {
    auto *m=meshAt(modelState(SELF,API),GET_NEXT_INT(ARGS));
    RETURN->v_int=m?static_cast<t_CKINT>(m->mNumBones):-1;
}
CK_DLL_MFUN(model3d_export_weights) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API); auto mesh=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto *m=meshAt(p,mesh);
        if(!m||!m->HasBones()||!a) return;
        uint64_t n=static_cast<uint64_t>(m->mNumVertices)*m->mNumBones;
        if(n>16000000||API->object->array_float_size(a)!=static_cast<t_CKINT>(n)) return;
        std::vector<double> data(n,0);
        for(unsigned b=0;b<m->mNumBones;++b) for(unsigned j=0;j<m->mBones[b]->mNumWeights;++j) {
            auto w=m->mBones[b]->mWeights[j];
            if(w.mVertexId>=m->mNumVertices||!unit(w.mWeight)) return;
            data[static_cast<size_t>(w.mVertexId)*m->mNumBones+b]+=w.mWeight;
        }
        for(size_t i=0;i<data.size();++i) API->object->array_float_set_idx(a,i,data[i]);
        RETURN->v_int=1;
    } catch(...) {}
}


namespace {
// Generated skeletons have identity bind transforms. Pose matrices are local
// to their parent, and weighted positions are evaluated without mutating rest vertices.
aiNode *skinNode(ModelState *p,t_CKINT mesh,t_CKINT bone) {
    auto *m=editableMesh(p,mesh);
    if(!m||bone<0||static_cast<uint64_t>(bone)>=m->mNumBones) return nullptr;
    return p->generated->mRootNode->FindNode(m->mBones[bone]->mName);
}
// Imported scenes remain immutable; sampled local transforms live in an overlay.
const aiNode *uniqueNode(const aiNode *root,const aiString &name) {
    const aiNode *found=nullptr;
    std::vector<const aiNode*> pending{root};
    while(!pending.empty()) {
        auto *n=pending.back(); pending.pop_back();
        if(n->mName==name) { if(found) return nullptr; found=n; }
        for(unsigned i=0;i<n->mNumChildren;++i) pending.push_back(n->mChildren[i]);
    }
    return found;
}
const aiNode *uniqueMeshNode(const aiNode *root,unsigned mesh) {
    const aiNode *found=nullptr;
    std::vector<const aiNode*> pending{root};
    while(!pending.empty()) {
        auto *n=pending.back(); pending.pop_back();
        for(unsigned i=0;i<n->mNumMeshes;++i) if(n->mMeshes[i]==mesh) {
            if(found) return nullptr; found=n;
        }
        for(unsigned i=0;i<n->mNumChildren;++i) pending.push_back(n->mChildren[i]);
    }
    return found;
}
bool finiteMatrix(const aiMatrix4x4 &m) {
    for(unsigned i=0;i<4;++i) for(unsigned j=0;j<4;++j)
        if(!coordinate(m[i][j])) return false;
    return true;
}
bool importedWorld(ModelState *p,const aiNode *node,aiMatrix4x4 &world) {
    if(!node) return false;
    world=aiMatrix4x4(); unsigned depth=0;
    while(node) {
        if(++depth>1024) return false;
        auto it=p->importedPose.find(node);
        world=(it==p->importedPose.end()?node->mTransformation:it->second)*world;
        if(!finiteMatrix(world)) return false;
        if(node==p->scene->mRootNode) return true;
        node=node->mParent;
    }
    return false;
}
bool skinWorld(ModelState *p,aiNode *node,aiMatrix4x4 &world) {
    auto *root=p->generated->mRootNode;
    world=aiMatrix4x4();
    unsigned depth=0;
    while(node&&node!=root) {
        if(++depth>64) return false;
        world=node->mTransformation*world;
        if(!finiteMatrix(world)) return false;
        node=node->mParent;
    }
    return node==root;
}
}
namespace {
struct SceneNodeEntry { const aiNode *node; t_CKINT parent; };
// Preorder indices are valid until the scene's topology changes.
bool sceneNodes(ModelState *p,std::vector<SceneNodeEntry> &out) {
    if(!p||!p->scene||!p->scene->mRootNode) return false;
    std::vector<SceneNodeEntry> pending{{p->scene->mRootNode,-1}};
    while(!pending.empty()) {
        auto entry=pending.back(); pending.pop_back();
        if(!entry.node||out.size()>=1000000) return false;
        auto index=static_cast<t_CKINT>(out.size());
        out.push_back(entry);
        for(unsigned j=entry.node->mNumChildren;j>0;--j)
            pending.push_back({entry.node->mChildren[j-1],index});
    }
    return true;
}
bool nodeIndex(const std::vector<SceneNodeEntry> &nodes,t_CKINT i) {
    return i>=0&&static_cast<uint64_t>(i)<nodes.size();
}
bool writeNodeMatrix(ModelState *p,t_CKINT index,Chuck_DL_Api::ArrayFloat a,
                     bool world,CK_DL_API API) {
    if(!a||API->object->array_float_size(a)!=16) return false;
    std::vector<SceneNodeEntry> nodes;
    if(!sceneNodes(p,nodes)||!nodeIndex(nodes,index)) return false;
    const auto *node=nodes[index].node;
    aiMatrix4x4 matrix;
    if(world) {
        // Imported-world helper also handles generated nodes (empty overlay).
        if(!importedWorld(p,node,matrix)) return false;
    } else {
        auto it=p->importedPose.find(node);
        matrix=it==p->importedPose.end()?node->mTransformation:it->second;
    }
    if(!finiteMatrix(matrix)) return false;
    for(unsigned r=0;r<4;++r)
        for(unsigned c=0;c<4;++c)
            API->object->array_float_set_idx(a,4*r+c,matrix[r][c]);
    return true;
}
}
CK_DLL_MFUN(model3d_node_count) {
    RETURN->v_int=-1;
    try {
        std::vector<SceneNodeEntry> nodes;
        if(sceneNodes(modelState(SELF,API),nodes)) RETURN->v_int=nodes.size();
    } catch(...) {}
}
CK_DLL_MFUN(model3d_node_parent) {
    RETURN->v_int=-2;
    try {
        auto index=GET_NEXT_INT(ARGS);
        std::vector<SceneNodeEntry> nodes;
        if(sceneNodes(modelState(SELF,API),nodes)&&nodeIndex(nodes,index))
            RETURN->v_int=nodes[index].parent;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_node_mesh_count) {
    RETURN->v_int=-1;
    try {
        auto index=GET_NEXT_INT(ARGS);
        std::vector<SceneNodeEntry> nodes;
        if(sceneNodes(modelState(SELF,API),nodes)&&nodeIndex(nodes,index))
            RETURN->v_int=nodes[index].node->mNumMeshes;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_node_mesh_at) {
    RETURN->v_int=-1;
    try {
        auto index=GET_NEXT_INT(ARGS),slot=GET_NEXT_INT(ARGS);
        auto *p=modelState(SELF,API);
        std::vector<SceneNodeEntry> nodes;
        if(!sceneNodes(p,nodes)||!nodeIndex(nodes,index)||slot<0) return;
        const auto *node=nodes[index].node;
        if(static_cast<uint64_t>(slot)>=node->mNumMeshes) return;
        auto mesh=node->mMeshes[slot];
        if(mesh<p->scene->mNumMeshes) RETURN->v_int=mesh;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_node_local_matrix) {
    RETURN->v_int=0;
    try {
        auto index=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        RETURN->v_int=writeNodeMatrix(modelState(SELF,API),index,a,false,API);
    } catch(...) {}
}
CK_DLL_MFUN(model3d_node_world_matrix) {
    RETURN->v_int=0;
    try {
        auto index=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        RETURN->v_int=writeNodeMatrix(modelState(SELF,API),index,a,true,API);
    } catch(...) {}
}
CK_DLL_MFUN(model3d_export_node_trs) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API);
        auto index=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        std::vector<SceneNodeEntry> nodes;
        if(!a||API->object->array_float_size(a)!=10||!sceneNodes(p,nodes)||!nodeIndex(nodes,index)) return;
        auto *node=nodes[index].node;
        auto it=p->importedPose.find(node);
        auto matrix=it==p->importedPose.end()?node->mTransformation:it->second;
        if(!finiteMatrix(matrix)) return;
        aiVector3D scale,pos;
        aiQuaternion rotation;
        matrix.Decompose(scale,rotation,pos);
        if(std::abs(scale.x)<1e-8||std::abs(scale.y)<1e-8||std::abs(scale.z)<1e-8) return;
        rotation.Normalize();
        // Reject shear and perspective instead of silently approximating them.
        aiMatrix4x4 t,s;
        aiMatrix4x4::Translation(pos,t);
        aiMatrix4x4::Scaling(scale,s);
        aiMatrix4x4 reconstructed=t*aiMatrix4x4(rotation.GetMatrix())*s;
        if(!finiteMatrix(reconstructed)) return;
        for(unsigned r=0;r<4;++r) for(unsigned c=0;c<4;++c)
            if(std::abs(double(matrix[r][c])-reconstructed[r][c])>
               1e-5*std::max(1.0,std::abs(double(matrix[r][c])))) return;
        double values[]={pos.x,pos.y,pos.z,rotation.x,rotation.y,rotation.z,rotation.w,scale.x,scale.y,scale.z};
        for(double v:values) if(!coordinate(v)) return;
        for(unsigned i=0;i<10;++i) API->object->array_float_set_idx(a,i,values[i]);
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_bone_parent) {
    RETURN->v_int=-2;
    auto *p=modelState(SELF,API);
    auto mesh=GET_NEXT_INT(ARGS),bone=GET_NEXT_INT(ARGS);
    auto *node=skinNode(p,mesh,bone);
    if(!node) return;
    if(node->mParent==p->generated->mRootNode) { RETURN->v_int=-1; return; }
    auto *m=editableMesh(p,mesh);
    for(unsigned i=0;i<m->mNumBones;++i)
        if(skinNode(p,mesh,i)==node->mParent) { RETURN->v_int=i; return; }
}
CK_DLL_MFUN(model3d_set_bone_parent) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API);
        auto mesh=GET_NEXT_INT(ARGS),bone=GET_NEXT_INT(ARGS),parent=GET_NEXT_INT(ARGS);
        auto *node=skinNode(p,mesh,bone);
        if(!node||parent< -1||parent==bone) return;
        auto *target=parent==-1?p->generated->mRootNode:skinNode(p,mesh,parent);
        if(!target) return;
        // Refuse cycles and preserve local transforms (not world-space pose).
        for(auto *q=target;q;q=q->mParent) if(q==node) return;
        auto *old=node->mParent;
        if(!old) return;
        if(old==target) { RETURN->v_int=1; return; }
        auto oldList=std::make_unique<aiNode*[]>(old->mNumChildren-1);
        auto newList=std::make_unique<aiNode*[]>(target->mNumChildren+1);
        unsigned n=0;
        for(unsigned i=0;i<old->mNumChildren;++i)
            if(old->mChildren[i]!=node) {
                if(n>=old->mNumChildren-1) return;
                oldList[n++]=old->mChildren[i];
            }
        if(n!=old->mNumChildren-1) return;
        for(unsigned i=0;i<target->mNumChildren;++i) newList[i]=target->mChildren[i];
        newList[target->mNumChildren]=node;
        delete[] old->mChildren; old->mChildren=oldList.release(); --old->mNumChildren;
        delete[] target->mChildren; target->mChildren=newList.release(); ++target->mNumChildren;
        node->mParent=target; RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_bone_pose) {
    RETURN->v_int=0;
    auto *p=modelState(SELF,API);
    auto mesh=GET_NEXT_INT(ARGS),bone=GET_NEXT_INT(ARGS);
    auto x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS),z=GET_NEXT_FLOAT(ARGS);
    auto ax=GET_NEXT_FLOAT(ARGS),ay=GET_NEXT_FLOAT(ARGS),az=GET_NEXT_FLOAT(ARGS);
    auto *node=skinNode(p,mesh,bone);
    if(!node||!coordinate(x)||!coordinate(y)||!coordinate(z)||
       !coordinate(ax)||!coordinate(ay)||!coordinate(az)) return;
    constexpr double rad=3.14159265358979323846/180.0;
    aiMatrix4x4 t,rx,ry,rz;
    aiMatrix4x4::Translation(aiVector3D(x,y,z),t);
    aiMatrix4x4::RotationX(std::remainder(ax,360.0)*rad,rx);
    aiMatrix4x4::RotationY(std::remainder(ay,360.0)*rad,ry);
    aiMatrix4x4::RotationZ(std::remainder(az,360.0)*rad,rz);
    node->mTransformation=t*rz*ry*rx;
    RETURN->v_int=1;
}
CK_DLL_MFUN(model3d_reset_pose) {
    RETURN->v_int=0;
    auto *p=modelState(SELF,API); auto mesh=GET_NEXT_INT(ARGS);
    auto *m=editableMesh(p,mesh);
    if(!m||!m->HasBones()) return;
    for(unsigned b=0;b<m->mNumBones;++b) if(!skinNode(p,mesh,b)) return;
    for(unsigned b=0;b<m->mNumBones;++b) skinNode(p,mesh,b)->mTransformation=aiMatrix4x4();
    RETURN->v_int=1;
}
namespace {
// One traversal of the skin weights and joint transforms, for either or both
// output streams. All validation precedes writes into ChucK arrays.
struct SkinSum { double x=0,y=0,z=0; };
bool deformSkin(ModelState *p,t_CKINT mesh,
                Chuck_DL_Api::ArrayFloat positions,Chuck_DL_Api::ArrayFloat normals,
                CK_DL_API API) {
    auto *m=meshAt(p,mesh);
    if(!m||!m->HasBones()||(!positions&&!normals)||
       (positions&&normals&&positions==normals)||
       (positions&&API->object->array_float_size(positions)!=static_cast<int64_t>(m->mNumVertices)*3)||
       (normals&&(!m->HasNormals()||API->object->array_float_size(normals)!=static_cast<int64_t>(m->mNumVertices)*3))) return false;
    aiMatrix4x4 inverseMesh;
    if(!p->generated) {
        if(!importedWorld(p,uniqueMeshNode(p->scene->mRootNode,static_cast<unsigned>(mesh)),inverseMesh)) return false;
        auto det=inverseMesh.Determinant();
        if(!std::isfinite(det)||det==0) return false;
        inverseMesh.Inverse();
        if(!finiteMatrix(inverseMesh)) return false;
    }
    // Allocate only the requested sums; joint transforms and weights are shared.
    std::vector<SkinSum> xyz(positions?m->mNumVertices:0),nrm(normals?m->mNumVertices:0);
    std::vector<double> weights(m->mNumVertices,0);
    auto accumulate=[](SkinSum &sum,const aiVector3D &v,double w) {
        sum.x+=double(v.x)*w; sum.y+=double(v.y)*w; sum.z+=double(v.z)*w;
    };
    for(unsigned b=0;b<m->mNumBones;++b) {
        aiMatrix4x4 world;
        if(p->generated) {
            if(!skinWorld(p,skinNode(p,mesh,b),world)) return false;
        } else {
            if(!importedWorld(p,uniqueNode(p->scene->mRootNode,m->mBones[b]->mName),world)) return false;
            world=inverseMesh*world;
        }
        world=world*m->mBones[b]->mOffsetMatrix;
        if(!finiteMatrix(world)) return false;
        aiMatrix3x3 normalMatrix;
        if(normals) {
            normalMatrix=aiMatrix3x3(world);
            auto det=normalMatrix.Determinant();
            if(!std::isfinite(det)||det==0) return false;
            normalMatrix.Inverse().Transpose();
        }
        for(unsigned j=0;j<m->mBones[b]->mNumWeights;++j) {
            auto weight=m->mBones[b]->mWeights[j];
            auto i=weight.mVertexId;
            double w=weight.mWeight;
            if(i>=m->mNumVertices||!unit(w)) return false;
            if(positions) accumulate(xyz[i],world*m->mVertices[i],w);
            if(normals) accumulate(nrm[i],normalMatrix*m->mNormals[i],w);
            weights[i]+=w;
        }
    }
    for(unsigned i=0;i<m->mNumVertices;++i) {
        double w=weights[i];
        if(w==0&&!p->generated) {
            if(positions) { auto v=m->mVertices[i]; xyz[i]={v.x,v.y,v.z}; }
            if(normals) { auto v=m->mNormals[i]; nrm[i]={v.x,v.y,v.z}; }
            w=1;
        }
        if(w<=0) return false;
        if(positions) {
            auto &q=xyz[i]; q.x/=w; q.y/=w; q.z/=w;
            if(!coordinate(q.x)||!coordinate(q.y)||!coordinate(q.z)) return false;
        }
        if(normals) {
            auto &q=nrm[i]; double len=std::hypot(q.x,q.y,q.z);
            if(!std::isfinite(len)||len<=1e-12) return false;
            q.x/=len; q.y/=len; q.z/=len;
        }
    }
    for(unsigned i=0;i<m->mNumVertices;++i) {
        if(positions) {
            const auto &q=xyz[i];
            API->object->array_float_set_idx(positions,3*i,q.x);
            API->object->array_float_set_idx(positions,3*i+1,q.y);
            API->object->array_float_set_idx(positions,3*i+2,q.z);
        }
        if(normals) {
            const auto &q=nrm[i];
            API->object->array_float_set_idx(normals,3*i,q.x);
            API->object->array_float_set_idx(normals,3*i+1,q.y);
            API->object->array_float_set_idx(normals,3*i+2,q.z);
        }
    }
    return true;
}
}
CK_DLL_MFUN(model3d_export_posed) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API); auto mesh=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(a) RETURN->v_int=deformSkin(p,mesh,a,nullptr,API);
    } catch(...) {}
}
CK_DLL_MFUN(model3d_export_posed_normals) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API); auto mesh=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(a) RETURN->v_int=deformSkin(p,mesh,nullptr,a,API);
    } catch(...) {}
}
CK_DLL_MFUN(model3d_export_posed_geometry) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API); auto mesh=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto b=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(a&&b) RETURN->v_int=deformSkin(p,mesh,a,b,API);
    } catch(...) {}
}


namespace {
const aiAnimation *animationAt(ModelState *p,t_CKINT index) {
    return p&&p->scene&&index>=0&&static_cast<uint64_t>(index)<p->scene->mNumAnimations
        ? p->scene->mAnimations[index] : nullptr;
}
}
// Generated clips use seconds as ticks and a shared time grid for all bones.
// Each frame stores six values per bone: local XYZ and Euler XYZ in degrees.
CK_DLL_MFUN(model3d_add_animation) {
    RETURN->v_int=-1;
    try {
        auto *p=modelState(SELF,API);
        auto mesh=GET_NEXT_INT(ARGS);
        auto name=GET_NEXT_STRING_SAFE(ARGS);
        auto times=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto poses=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto *m=editableMesh(p,mesh);
        if(!m||!m->HasBones()||!times||!poses||name.empty()||name.size()>255||
           name.find('\0')!=std::string::npos||p->generated->mNumAnimations>=256) return;
        auto count=API->object->array_float_size(times);
        if(count<2||count>10000||uint64_t(count)*m->mNumBones*6>6000000||
           API->object->array_float_size(poses)!=count*m->mNumBones*6) return;
        auto *scene=p->generated.get();
        for(unsigned i=0;i<scene->mNumAnimations;++i)
            if(scene->mAnimations[i]->mName==aiString(name)) return;
        std::vector<double> t(count);
        for(unsigned i=0;i<t.size();++i) {
            t[i]=API->object->array_float_get_idx(times,i);
            if(!std::isfinite(t[i])||t[i]>1e6||
               (i==0?t[i]!=0:t[i]<=t[i-1])) return;
        }
        auto clip=std::make_unique<aiAnimation>();
        clip->mName=aiString(name); clip->mTicksPerSecond=1; clip->mDuration=t.back();
        clip->mChannels=new aiNodeAnim*[m->mNumBones]();
        clip->mNumChannels=m->mNumBones;
        constexpr double rad=3.14159265358979323846/180.0;
        for(unsigned b=0;b<m->mNumBones;++b) {
            if(!skinNode(p,mesh,b)) return;
            auto channel=std::make_unique<aiNodeAnim>();
            channel->mNodeName=m->mBones[b]->mName;
            channel->mPositionKeys=new aiVectorKey[count];
            channel->mRotationKeys=new aiQuatKey[count];
            channel->mScalingKeys=new aiVectorKey[count];
            channel->mNumPositionKeys=channel->mNumRotationKeys=channel->mNumScalingKeys=count;
            for(unsigned k=0;k<t.size();++k) {
                double v[6];
                for(unsigned j=0;j<6;++j) {
                    v[j]=API->object->array_float_get_idx(poses,(uint64_t(k)*m->mNumBones+b)*6+j);
                    if(!coordinate(v[j])) return;
                }
                aiMatrix4x4 rx,ry,rz;
                aiMatrix4x4::RotationX(std::remainder(v[3],360.0)*rad,rx);
                aiMatrix4x4::RotationY(std::remainder(v[4],360.0)*rad,ry);
                aiMatrix4x4::RotationZ(std::remainder(v[5],360.0)*rad,rz);
                auto q=aiQuaternion(aiMatrix3x3(rz*ry*rx));
                q.Normalize();
                if(k) {
                    const auto &prev=channel->mRotationKeys[k-1].mValue;
                    if(prev.w*q.w+prev.x*q.x+prev.y*q.y+prev.z*q.z<0)
                        q=aiQuaternion(-q.w,-q.x,-q.y,-q.z);
                }
                channel->mPositionKeys[k]=aiVectorKey(t[k],aiVector3D(v[0],v[1],v[2]));
                channel->mRotationKeys[k]=aiQuatKey(t[k],q);
                channel->mScalingKeys[k]=aiVectorKey(t[k],aiVector3D(1,1,1));
            }
            clip->mChannels[b]=channel.release();
        }
        auto list=std::make_unique<aiAnimation*[]>(scene->mNumAnimations+1);
        for(unsigned i=0;i<scene->mNumAnimations;++i) list[i]=scene->mAnimations[i];
        list[scene->mNumAnimations]=clip.release();
        delete[] scene->mAnimations; scene->mAnimations=list.release();
        RETURN->v_int=scene->mNumAnimations++;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_animations) {
    auto *p=modelState(SELF,API);
    RETURN->v_int=p&&p->scene?static_cast<t_CKINT>(p->scene->mNumAnimations):-1;
}
CK_DLL_MFUN(model3d_animation_duration) {
    auto *p=modelState(SELF,API);
    auto *a=animationAt(p,GET_NEXT_INT(ARGS));
    RETURN->v_float=a&&a->mTicksPerSecond>0?a->mDuration/a->mTicksPerSecond:-1;
}
namespace {
template<class Key,class Value,class Interpolate>
bool sampleKeys(const Key *keys,unsigned n,double t,Value &out,Interpolate interpolate) {
    if(!n||!keys) return false;
    for(unsigned i=0;i<n;++i)
        if(!std::isfinite(keys[i].mTime)||(i&&keys[i].mTime<=keys[i-1].mTime)) return false;
    if(t<=keys[0].mTime) out=keys[0].mValue;
    else if(t>=keys[n-1].mTime) out=keys[n-1].mValue;
    else {
        auto hi=std::upper_bound(keys,keys+n,t,[](double v,const Key &k){return v<k.mTime;});
        auto lo=hi-1;
        out=interpolate(lo->mValue,hi->mValue,static_cast<ai_real>((t-lo->mTime)/(hi->mTime-lo->mTime)));
    }
    return true;
}
bool validQuaternion(const aiQuaternion &q) {
    double n=double(q.w)*q.w+double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z;
    return std::isfinite(n)&&n>0;
}
struct LocalTRS { aiVector3D position; aiQuaternion rotation; aiVector3D scale; };
using ClipPose=std::unordered_map<const aiNode*,LocalTRS>;
using MatrixPose=std::unordered_map<const aiNode*,aiMatrix4x4>;
aiMatrix4x4 composeTRS(const LocalTRS &v) {
    aiMatrix4x4 m(v.rotation.GetMatrix()),s;
    aiMatrix4x4::Scaling(v.scale,s); m=m*s;
    m.a4=v.position.x; m.b4=v.position.y; m.c4=v.position.z;
    return m;
}
bool evaluateClip(ModelState *p,t_CKINT index,double seconds,t_CKINT loop,ClipPose &next) {
        auto *a=animationAt(p,index);
        if(!a||!std::isfinite(seconds)||(loop!=0&&loop!=1)||
           !std::isfinite(a->mDuration)||a->mDuration<=0||
           !std::isfinite(a->mTicksPerSecond)||a->mTicksPerSecond<=0||
           !a->mNumChannels||a->mNumMeshChannels||a->mNumMorphMeshChannels) return false;
        double duration=a->mDuration/a->mTicksPerSecond;
        if(!std::isfinite(duration)||duration<=0) return false;
        double t=seconds;
        if(loop) { t=std::fmod(t,duration); if(t<0) t+=duration; }
        else t=std::clamp(t,0.0,duration);
        t*=a->mTicksPerSecond;
        auto linear=[](const aiVector3D &x,const aiVector3D &y,ai_real u){return x+(y-x)*u;};
        auto spherical=[](const aiQuaternion &x,const aiQuaternion &y,ai_real u){
            aiQuaternion q; aiQuaternion::Interpolate(q,x,y,u); return q;
        };
        for(unsigned i=0;i<a->mNumChannels;++i) {
            auto *c=a->mChannels[i];
            if(!c) return false;
            auto *node=uniqueNode(p->scene->mRootNode,c->mNodeName);
            if(!node) return false;
            aiVector3D v,scale;
            aiQuaternion q;
            if(!sampleKeys(c->mPositionKeys,c->mNumPositionKeys,t,v,linear)||
               !sampleKeys(c->mRotationKeys,c->mNumRotationKeys,t,q,spherical)||
               !sampleKeys(c->mScalingKeys,c->mNumScalingKeys,t,scale,linear)||
               !validQuaternion(q)) return false;
            q.Normalize();
            aiMatrix4x4 matrix(q.GetMatrix()),s;
            aiMatrix4x4::Scaling(scale,s);
            matrix=matrix*s;
            matrix.a4=v.x; matrix.b4=v.y; matrix.c4=v.z;
            if(!finiteMatrix(matrix)||!next.emplace(node,LocalTRS{v,q,scale}).second) return false;
        }
    return true;
}
void applyClip(ModelState *p,MatrixPose &next) {
    if(p->generated) {
        for(const auto &item:next) const_cast<aiNode*>(item.first)->mTransformation=item.second;
    } else p->importedPose.swap(next);
}
}
CK_DLL_MFUN(model3d_sample_animation) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API);
        auto index=GET_NEXT_INT(ARGS);
        double seconds=GET_NEXT_FLOAT(ARGS);
        auto loop=GET_NEXT_INT(ARGS);
        ClipPose pose; MatrixPose next;
        if(!evaluateClip(p,index,seconds,loop,pose)) return;
        for(const auto &item:pose) next.emplace(item.first,composeTRS(item.second));
        applyClip(p,next);
        p->playback=ModelState::Playback{};
        RETURN->v_int=1;
    } catch(...) {}
}
// Blend local TRS, never already-skinned positions or matrix coefficients.
// Require matching channel sets so missing tracks cannot depend on prior pose.
CK_DLL_MFUN(model3d_blend_animations) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API);
        auto first=GET_NEXT_INT(ARGS);
        double firstTime=GET_NEXT_FLOAT(ARGS);
        auto second=GET_NEXT_INT(ARGS);
        double secondTime=GET_NEXT_FLOAT(ARGS);
        double weight=GET_NEXT_FLOAT(ARGS);
        auto loop=GET_NEXT_INT(ARGS);
        if(!unit(weight)) return;
        ClipPose x,y; MatrixPose next;
        if(!evaluateClip(p,first,firstTime,loop,x)||!evaluateClip(p,second,secondTime,loop,y)||x.size()!=y.size()) return;
        ai_real u=static_cast<ai_real>(weight);
        for(const auto &item:x) {
            auto it=y.find(item.first);
            if(it==y.end()) return;
            const auto &a=item.second; const auto &b=it->second;
            LocalTRS v;
            v.position=a.position+(b.position-a.position)*u;
            v.scale=a.scale+(b.scale-a.scale)*u;
            aiQuaternion::Interpolate(v.rotation,a.rotation,b.rotation,u);
            if(!validQuaternion(v.rotation)) return;
            v.rotation.Normalize();
            auto matrix=composeTRS(v);
            if(!finiteMatrix(matrix)) return;
            next.emplace(item.first,matrix);
        }
        applyClip(p,next);
        p->playback=ModelState::Playback{};
        RETURN->v_int=1;
    } catch(...) {}
}
namespace {
double playbackDuration(ModelState *p,t_CKINT clip) {
    auto *a=animationAt(p,clip);
    if(!a||!std::isfinite(a->mDuration)||!std::isfinite(a->mTicksPerSecond)||
       a->mDuration<=0||a->mTicksPerSecond<=0) return -1;
    double d=a->mDuration/a->mTicksPerSecond;
    return std::isfinite(d)&&d>0?d:-1;
}
double playbackCursor(double t,double d,t_CKINT loop) {
    if(!loop) return std::clamp(t,0.0,d);
    double u=std::fmod(t,d); return u<0?u+d:u;
}
bool playbackPose(ModelState *p,const ModelState::Playback &v) {
    ClipPose a,b; MatrixPose next;
    if(!evaluateClip(p,v.clip,v.cursor,v.loop,a)) return false;
    if(v.target>=0) {
        if(!evaluateClip(p,v.target,v.targetCursor,v.loop,b)||a.size()!=b.size()) return false;
        auto u=static_cast<ai_real>(std::clamp(v.elapsed/v.fade,0.0,1.0));
        for(auto &item:a) {
            auto it=b.find(item.first); if(it==b.end()) return false;
            auto &x=item.second; const auto &y=it->second;
            x.position=x.position+(y.position-x.position)*u;
            x.scale=x.scale+(y.scale-x.scale)*u;
            aiQuaternion q;
            aiQuaternion::Interpolate(q,x.rotation,y.rotation,u);
            if(!validQuaternion(q)) return false;
            q.Normalize(); x.rotation=q;
        }
    }
    for(const auto &item:a) {
        auto matrix=composeTRS(item.second);
        if(!finiteMatrix(matrix)) return false;
        next.emplace(item.first,matrix);
    }
    applyClip(p,next); return true;
}
}
CK_DLL_MFUN(model3d_play) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API);
        auto clip=GET_NEXT_INT(ARGS),loop=GET_NEXT_INT(ARGS);
        if(!p||(loop!=0&&loop!=1)||playbackDuration(p,clip)<=0) return;
        ModelState::Playback v; v.clip=clip; v.loop=loop;
        if(!playbackPose(p,v)) return;
        p->playback=v; RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_playback_speed) {
    RETURN->v_int=0;
    auto *p=modelState(SELF,API); double speed=GET_NEXT_FLOAT(ARGS);
    if(!p||p->playback.clip<0||!std::isfinite(speed)||speed<0.0625||speed>16) return;
    p->playback.speed=speed; RETURN->v_int=1;
}
CK_DLL_MFUN(model3d_playback_pause) {
    RETURN->v_int=0;
    auto *p=modelState(SELF,API); auto paused=GET_NEXT_INT(ARGS);
    if(!p||p->playback.clip<0||(paused!=0&&paused!=1)) return;
    p->playback.paused=paused; RETURN->v_int=1;
}
CK_DLL_MFUN(model3d_playback_seek) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API); double position=GET_NEXT_FLOAT(ARGS);
        if(!p||p->playback.clip<0||p->playback.target>=0||!std::isfinite(position)) return;
        auto v=p->playback; double d=playbackDuration(p,v.clip);
        if(d<=0) return;
        v.cursor=playbackCursor(position,d,v.loop); v.ended=!v.loop&&v.cursor>=d;
        if(!playbackPose(p,v)) return;
        p->playback=v; RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_playback_fade) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API);
        auto clip=GET_NEXT_INT(ARGS); double fade=GET_NEXT_FLOAT(ARGS);
        if(!p||p->playback.clip<0||p->playback.target>=0||clip==p->playback.clip||
           !std::isfinite(fade)||fade<=0||fade>3600||playbackDuration(p,clip)<=0) return;
        auto v=p->playback;
        v.target=clip; v.targetCursor=0; v.fade=fade; v.elapsed=0; v.ended=0;
        if(!playbackPose(p,v)) return;
        p->playback=v; RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_playback_advance) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API); double delta=GET_NEXT_FLOAT(ARGS);
        if(!p||p->playback.clip<0||!std::isfinite(delta)||delta<0||delta>86400) return;
        auto v=p->playback;
        if(v.paused||v.ended) { RETURN->v_int=1; return; }
        double d=playbackDuration(p,v.clip);
        if(d<=0) return;
        v.cursor=playbackCursor(v.cursor+delta*v.speed,d,v.loop);
        if(v.target>=0) {
            double targetDuration=playbackDuration(p,v.target); if(targetDuration<=0) return;
            v.targetCursor=playbackCursor(v.targetCursor+delta*v.speed,targetDuration,v.loop);
            v.elapsed=std::min(v.fade,v.elapsed+delta);
            if(v.elapsed>=v.fade) {
                v.clip=v.target; v.cursor=v.targetCursor; v.target=-1;
                v.fade=0; v.elapsed=0; v.ended=!v.loop&&v.cursor>=targetDuration;
            }
        } else v.ended=!v.loop&&v.cursor>=d;
        if(!playbackPose(p,v)) return;
        p->playback=v; RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_playback_time) {
    auto *p=modelState(SELF,API);
    RETURN->v_float=p&&p->playback.clip>=0?p->playback.cursor:-1;
}
CK_DLL_MFUN(model3d_playback_clip) {
    auto *p=modelState(SELF,API); RETURN->v_int=p?p->playback.clip:-1;
}
CK_DLL_MFUN(model3d_playback_ended) {
    auto *p=modelState(SELF,API); RETURN->v_int=p&&p->playback.clip>=0&&p->playback.ended;
}
CK_DLL_MFUN(model3d_playback_stop) {
    auto *p=modelState(SELF,API);
    if(p) p->playback=ModelState::Playback{};
}
CK_DLL_MFUN(model3d_reset_animation) {
    RETURN->v_int=0;
    auto *p=modelState(SELF,API);
    if(!p||!p->scene||p->generated) return;
    p->importedPose.clear(); p->playback=ModelState::Playback{}; RETURN->v_int=1;
}

CK_DLL_MFUN(model3d_create_silhouette) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        auto threshold=GET_NEXT_INT(ARGS);
        double depth=GET_NEXT_FLOAT(ARGS);
        if(!p||!a||w<1||w>256||h<1||h>256||threshold<1||threshold>255||
           !std::isfinite(depth)||depth<0.0001||depth>1000000||
           API->object->array_int_size(a)!=w*h) return;
        std::vector<unsigned char> mask(w*h);
        for(size_t i=0;i<mask.size();++i) {
            auto v=API->object->array_int_get_idx(a,i);
            if(v<0||v>255) return;
            mask[i]=v>=threshold;
        }
        auto solid=[&](int x,int y){return x>=0&&x<w&&y>=0&&y<h&&mask[y*w+x];};
        std::vector<aiVector3D> verts,normals;
        std::vector<unsigned> indices;
        auto quad=[&](aiVector3D a,aiVector3D b,aiVector3D c,aiVector3D d,aiVector3D n) {
            unsigned i=static_cast<unsigned>(verts.size());
            verts.insert(verts.end(),{a,b,c,d});
            normals.insert(normals.end(),4,n);
            indices.insert(indices.end(),{i,i+1,i+2,i,i+2,i+3});
        };
        ai_real z=static_cast<ai_real>(depth/2);
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) if(solid(x,y)) {
            ai_real l=x-w/2.0,r=l+1,t=h/2.0-y,b=t-1;
            aiVector3D A(l,b,z),B(r,b,z),C(r,t,z),D(l,t,z);
            aiVector3D E(l,b,-z),F(r,b,-z),G(r,t,-z),H(l,t,-z);
            quad(A,B,C,D,{0,0,1}); quad(E,H,G,F,{0,0,-1});
            if(!solid(x-1,y)) quad(E,A,D,H,{-1,0,0});
            if(!solid(x+1,y)) quad(B,F,G,C,{1,0,0});
            if(!solid(x,y-1)) quad(D,C,G,H,{0,1,0});
            if(!solid(x,y+1)) quad(E,F,B,A,{0,-1,0});
            if(verts.size()>1000000||indices.size()>3000000) return;
        }
        if(verts.empty()) return;
        auto shading=std::make_unique<aiVector3D[]>(normals.size());
        std::copy(normals.begin(),normals.end(),shading.get());
        if(!makeMesh(p,verts,indices)) return;
        p->generated->mMeshes[0]->mNormals=shading.release();
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_create_heightmap) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        double step=GET_NEXT_FLOAT(ARGS),scale=GET_NEXT_FLOAT(ARGS);
        if(!p||!a||w<2||h<2||w>512||h>512||
           !std::isfinite(step)||step<0.0001||step>1000000||
           !std::isfinite(scale)||scale<0||scale>1000000||
           API->object->array_float_size(a)!=w*h) return;
        size_t n=w*h;
        std::vector<aiVector3D> verts;
        verts.reserve(n);
        auto uv=std::make_unique<aiVector3D[]>(n);
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            size_t i=y*w+x;
            double v=API->object->array_float_get_idx(a,i);
            if(!std::isfinite(v)||v<0||v>1) return;
            verts.emplace_back((x-(w-1)*0.5)*step,((h-1)*0.5-y)*step,v*scale);
            uv[i]=aiVector3D(double(x)/(w-1),1.0-double(y)/(h-1),0);
        }
        std::vector<unsigned> indices;
        indices.reserve(6*(w-1)*(h-1));
        for(int y=0;y<h-1;++y) for(int x=0;x<w-1;++x) {
            unsigned i=y*w+x,j=i+static_cast<unsigned>(w);
            indices.insert(indices.end(),{i,j,i+1,i+1,j,j+1});
        }
        struct Sum { double x=0,y=0,z=0; };
        std::vector<Sum> sums(n);
        for(size_t i=0;i<indices.size();i+=3) {
            auto A=verts[indices[i]],B=verts[indices[i+1]],C=verts[indices[i+2]];
            double ux=double(B.x)-A.x,uy=double(B.y)-A.y,uz=double(B.z)-A.z;
            double vx=double(C.x)-A.x,vy=double(C.y)-A.y,vz=double(C.z)-A.z;
            Sum cross{uy*vz-uz*vy,uz*vx-ux*vz,ux*vy-uy*vx};
            for(size_t k=i;k<i+3;++k) {
                auto &s=sums[indices[k]];
                s.x+=cross.x; s.y+=cross.y; s.z+=cross.z;
            }
        }
        auto normals=std::make_unique<aiVector3D[]>(n);
        for(size_t i=0;i<n;++i) {
            auto s=sums[i]; double len=std::hypot(s.x,s.y,s.z);
            if(!std::isfinite(len)||len<=0) return;
            normals[i]=aiVector3D(s.x/len,s.y/len,s.z/len);
        }
        if(!makeMesh(p,verts,indices)) return;
        auto *mesh=p->generated->mMeshes[0];
        mesh->mNormals=normals.release();
        mesh->mTextureCoords[0]=uv.release(); mesh->mNumUVComponents[0]=2;
        RETURN->v_int=1;
    } catch(...) {}
}
namespace {
void renderOrthographicFrame(Chuck_Object *SELF, CK_DL_API API, void *ARGS, Chuck_DL_Return *RETURN, bool opaque) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API);
        auto index=GET_NEXT_INT(ARGS),w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        double span=GET_NEXT_FLOAT(ARGS);
        auto rgb=GET_NEXT_INT(ARGS);
        auto background=opaque?GET_NEXT_INT(ARGS):0;
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        auto *m=meshAt(p,index);
        if(!m||!a||w<1||h<1||w>2048||h>2048||w*h>4194304||
           !std::isfinite(span)||span<0.0001||span>1e9||rgb<0||rgb>0xffffff||background<0||background>0xffffff||
           m->mNumVertices>1000000||m->mNumFaces>1000000||
           API->object->array_int_size(a)!=w*h*4) return;
        aiMatrix4x4 t,rx,ry,rz,s;
        constexpr double rad=3.14159265358979323846/180.0;
        auto &c=p->controls;
        aiMatrix4x4::Translation(aiVector3D(c[0],c[1],c[2]),t);
        aiMatrix4x4::RotationX(static_cast<ai_real>(std::remainder(c[3],360.0)*rad),rx);
        aiMatrix4x4::RotationY(static_cast<ai_real>(std::remainder(c[4],360.0)*rad),ry);
        aiMatrix4x4::RotationZ(static_cast<ai_real>(std::remainder(c[5],360.0)*rad),rz);
        aiMatrix4x4::Scaling(aiVector3D(c[6],c[7],c[8]),s);
        auto transform=t*rz*ry*rx*s;
        struct V { double x,y,z; };
        std::vector<V> verts;
        verts.reserve(m->mNumVertices);
        double factor=h/span;
        for(unsigned i=0;i<m->mNumVertices;++i) {
            auto v=transform*m->mVertices[i];
            if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)) return;
            verts.push_back({w*0.5+v.x*factor,h*0.5-v.y*factor,v.z});
        }
        auto edge=[](V a,V b,double x,double y) {
            return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);
        };
        std::vector<double> depth(w*h,-std::numeric_limits<double>::infinity());
        std::vector<unsigned char> pixels(w*h*4,0);
        if(opaque) for(size_t i=0;i<pixels.size();i+=4) {
            for(int c=0;c<3;++c) pixels[i+c]=(background>>(16-8*c))&255;
            pixels[i+3]=255;
        }
        uint64_t work=0;
        for(unsigned f=0;f<m->mNumFaces;++f) {
            const auto &face=m->mFaces[f];
            if(face.mNumIndices!=3) return;
            for(unsigned k=0;k<3;++k) if(face.mIndices[k]>=verts.size()) return;
            V A=verts[face.mIndices[0]],B=verts[face.mIndices[1]],C=verts[face.mIndices[2]];
            double area=edge(A,B,C.x,C.y);
            if(!std::isfinite(area)||area==0) continue;
            double minX=std::min({A.x,B.x,C.x}),maxX=std::max({A.x,B.x,C.x});
            double minY=std::min({A.y,B.y,C.y}),maxY=std::max({A.y,B.y,C.y});
            if(maxX<0.5||maxY<0.5||minX>w-0.5||minY>h-0.5) continue;
            int x0=static_cast<int>(std::max(0.0,std::ceil(minX-0.5)));
            int y0=static_cast<int>(std::max(0.0,std::ceil(minY-0.5)));
            int x1=static_cast<int>(std::min(double(w-1),std::floor(maxX-0.5)));
            int y1=static_cast<int>(std::min(double(h-1),std::floor(maxY-0.5)));
            if(x1<x0||y1<y0) continue;
            work+=uint64_t(x1-x0+1)*(y1-y0+1);
            if(work>100000000) return;
            double ux=(B.x-A.x)/factor,uy=-(B.y-A.y)/factor,uz=B.z-A.z;
            double vx=(C.x-A.x)/factor,vy=-(C.y-A.y)/factor,vz=C.z-A.z;
            double nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
            double len=std::hypot(nx,ny,nz);
            if(!std::isfinite(len)||len<=0) continue;
            double light=0.25+0.75*std::clamp(nz/len,0.0,1.0);
            unsigned char colors[3];
            for(int k=0;k<3;++k) colors[k]=static_cast<unsigned char>(
                std::lround(((rgb>>(16-8*k))&255)*light));
            for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) {
                double px=x+0.5,py=y+0.5;
                double u=edge(B,C,px,py)/area,v=edge(C,A,px,py)/area,q=edge(A,B,px,py)/area;
                if(u< -1e-12||v< -1e-12||q< -1e-12) continue;
                double z=u*A.z+v*B.z+q*C.z;
                size_t i=static_cast<size_t>(y)*w+x;
                if(z<=depth[i]) continue;
                depth[i]=z;
                for(int k=0;k<3;++k) pixels[4*i+k]=colors[k];
                pixels[4*i+3]=255;
            }
        }
        for(size_t i=0;i<pixels.size();++i) API->object->array_int_set_idx(a,i,pixels[i]);
        RETURN->v_int=1;
    } catch(...) {}
}
}
CK_DLL_MFUN(model3d_render_orthographic) {
    renderOrthographicFrame(SELF,API,ARGS,RETURN,false);
}
CK_DLL_MFUN(model3d_render_orthographic_opaque) {
    renderOrthographicFrame(SELF,API,ARGS,RETURN,true);
}
CK_DLL_MFUN(model3d_project_triangles) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API);
        auto index=GET_NEXT_INT(ARGS),w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        double span=GET_NEXT_FLOAT(ARGS);
        auto rgb=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto *m=meshAt(p,index);
        if(!m||!a||w<1||h<1||w>4096||h>4096||
           !std::isfinite(span)||span<0.0001||span>1e9||rgb<0||rgb>0xffffff||
           m->mNumVertices>1000000||m->mNumFaces<1||m->mNumFaces>4096||
           API->object->array_float_size(a)!=static_cast<t_CKINT>(m->mNumFaces)*7) return;
        aiMatrix4x4 t,rx,ry,rz,s;
        constexpr double rad=3.14159265358979323846/180.0;
        auto &c=p->controls;
        aiMatrix4x4::Translation(aiVector3D(c[0],c[1],c[2]),t);
        aiMatrix4x4::RotationX(static_cast<ai_real>(std::remainder(c[3],360.0)*rad),rx);
        aiMatrix4x4::RotationY(static_cast<ai_real>(std::remainder(c[4],360.0)*rad),ry);
        aiMatrix4x4::RotationZ(static_cast<ai_real>(std::remainder(c[5],360.0)*rad),rz);
        aiMatrix4x4::Scaling(aiVector3D(c[6],c[7],c[8]),s);
        auto transform=t*rz*ry*rx*s;
        struct V { double x,y,z; };
        std::vector<V> verts;
        verts.reserve(m->mNumVertices);
        double factor=h/span;
        for(unsigned i=0;i<m->mNumVertices;++i) {
            auto v=transform*m->mVertices[i];
            double x=w*0.5+v.x*factor,y=h*0.5-v.y*factor;
            if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>1000000||std::abs(y)>1000000||!std::isfinite(v.z)) return;
            verts.push_back({x,y,v.z});
        }
        struct Triangle { double depth; std::array<double,7> values; };
        std::vector<Triangle> out;
        out.reserve(m->mNumFaces);
        for(unsigned f=0;f<m->mNumFaces;++f) {
            const auto &face=m->mFaces[f];
            if(face.mNumIndices!=3) return;
            for(unsigned k=0;k<3;++k) if(face.mIndices[k]>=verts.size()) return;
            V A=verts[face.mIndices[0]],B=verts[face.mIndices[1]],C=verts[face.mIndices[2]];
            double ux=(B.x-A.x)/factor,uy=-(B.y-A.y)/factor,uz=B.z-A.z;
            double vx=(C.x-A.x)/factor,vy=-(C.y-A.y)/factor,vz=C.z-A.z;
            double nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
            double len=std::hypot(nx,ny,nz);
            if(!std::isfinite(len)) return;
            double light=0.25+0.75*(len>0?std::clamp(nz/len,0.0,1.0):0.0);
            unsigned color=0;
            for(int k=0;k<3;++k) color=(color<<8)|static_cast<unsigned>(
                std::lround(((rgb>>(16-8*k))&255)*light));
            out.push_back({(A.z+B.z+C.z)/3.0,{A.x,A.y,B.x,B.y,C.x,C.y,double(color)}});
        }
        std::stable_sort(out.begin(),out.end(),[](const Triangle &a,const Triangle &b){
            return a.depth<b.depth;
        });
        for(size_t i=0;i<out.size();++i) for(size_t k=0;k<7;++k)
            API->object->array_float_set_idx(a,7*i+k,out[i].values[k]);
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_export_audio) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API);
        auto index=GET_NEXT_INT(ARGS),axis=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto *m=meshAt(p,index);
        if(!m||!a||axis<0||axis>2||m->mNumVertices<1||m->mNumVertices>1000000) return;
        auto count=API->object->array_float_size(a);
        if(count<1||count>33554432) return;
        std::vector<double> values;
        values.reserve(m->mNumVertices);
        double lo=std::numeric_limits<double>::infinity(),hi=-lo;
        for(unsigned i=0;i<m->mNumVertices;++i) {
            const auto &v=m->mVertices[i];
            double sample=axis==0?v.x:axis==1?v.y:v.z;
            if(!std::isfinite(sample)) return;
            lo=std::min(lo,sample); hi=std::max(hi,sample);
            values.push_back(sample);
        }
        // Normalize coordinate range to bipolar PCM; constant axes are silent.
        double mid=lo*0.5+hi*0.5,half=hi*0.5-lo*0.5;
        for(auto &v:values) v=half>0?std::clamp((v-mid)/half,-1.0,1.0):0.0;
        // Uniformly sample the vertex sequence, preserving both endpoints.
        for(t_CKINT i=0;i<count;++i) {
            double position=count==1?0.0:double(i)*(values.size()-1)/(count-1);
            size_t left=std::min(static_cast<size_t>(position),values.size()-1);
            size_t right=std::min(left+1,values.size()-1);
            double mix=position-left;
            double sample=values[left]*(1.0-mix)+values[right]*mix;
            API->object->array_float_set_idx(a,i,std::clamp(sample,-1.0,1.0));
        }
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_export_pitches) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API);
        auto index=GET_NEXT_INT(ARGS),axis=GET_NEXT_INT(ARGS);
        auto low=GET_NEXT_INT(ARGS),high=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        auto *m=meshAt(p,index);
        if(!m||!a||axis<0||axis>2||low<0||high>127||low>high||
           m->mNumVertices<1||m->mNumVertices>1000000) return;
        auto n=API->object->array_int_size(a);
        if(n<1||n>100000) return;
        std::vector<double> values;
        values.reserve(m->mNumVertices);
        double lo=std::numeric_limits<double>::infinity(),hi=-lo;
        for(unsigned i=0;i<m->mNumVertices;++i) {
            const auto &v=m->mVertices[i];
            double x=axis==0?v.x:axis==1?v.y:v.z;
            if(!std::isfinite(x)) return;
            lo=std::min(lo,x); hi=std::max(hi,x);
            values.push_back(x);
        }
        double mid=lo*0.5+hi*0.5,half=hi*0.5-lo*0.5;
        for(auto &v:values)
            v=half>0?std::clamp(0.5+0.5*((v-mid)/half),0.0,1.0):0.5;
        for(t_CKINT i=0;i<n;++i) {
            double pos=n==1?0.0:double(i)*(values.size()-1)/(n-1);
            size_t left=std::min(static_cast<size_t>(pos),values.size()-1);
            size_t right=std::min(left+1,values.size()-1);
            double mix=pos-left;
            double v=values[left]*(1-mix)+values[right]*mix;
            API->object->array_int_set_idx(a,i,low+std::lround(std::clamp(v,0.0,1.0)*(high-low)));
        }
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_create_boxes) {
    RETURN->v_int=0;
    try {
        auto *p=modelState(SELF,API);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(!p||!a) return;
        auto n=API->object->array_float_size(a);
        if(n<6||n>4096*6||n%6) return;
        std::vector<aiVector3D> vertices;
        std::vector<unsigned> indices;
        vertices.reserve(n/6*24); indices.reserve(n/6*36);
        // Separate vertices per face retain sharp edges and face normals.
        constexpr unsigned corners[6][4]={{0,3,2,1},{4,5,6,7},{0,1,5,4},
                                         {3,7,6,2},{0,4,7,3},{1,2,6,5}};
        const aiVector3D normals[6]={{0,0,-1},{0,0,1},{0,-1,0},{0,1,0},{-1,0,0},{1,0,0}};
        auto ns=std::make_unique<aiVector3D[]>(n/6*24);
        auto uv=std::make_unique<aiVector3D[]>(n/6*24);
        const aiVector3D faceUV[4]={{0,0,0},{1,0,0},{1,1,0},{0,1,0}};
        for(t_CKINT i=0;i<n;i+=6) {
            double v[6];
            for(int k=0;k<6;++k) {
                v[k]=API->object->array_float_get_idx(a,i+k);
                if(!coordinate(v[k])) return;
            }
            for(int k=0;k<3;++k)
                if(!(static_cast<ai_real>(v[k])<static_cast<ai_real>(v[k+3]))) return;
            ai_real x0=v[0],y0=v[1],z0=v[2],x1=v[3],y1=v[4],z1=v[5];
            aiVector3D box[8]={{x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},
                                {x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}};
            for(unsigned f=0;f<6;++f) {
                unsigned offset=static_cast<unsigned>(vertices.size());
                for(unsigned k=0;k<4;++k) {
                    vertices.push_back(box[corners[f][k]]);
                    ns[offset+k]=normals[f]; uv[offset+k]=faceUV[k];
                }
                indices.insert(indices.end(),{offset,offset+1,offset+2,offset,offset+2,offset+3});
            }
        }
        if(!makeMesh(p,vertices,indices)) return;
        auto *mesh=p->generated->mMeshes[0];
        mesh->mNormals=ns.release(); mesh->mTextureCoords[0]=uv.release();
        mesh->mNumUVComponents[0]=2;
        RETURN->v_int=1;
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
    QUERY->add_mfun(QUERY,model3d_export_pitches,"int","exportPitches");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"int","axis");
    QUERY->add_arg(QUERY,"int","lowPitch");
    QUERY->add_arg(QUERY,"int","highPitch");
    QUERY->add_arg(QUERY,"int[]","pitches");
    QUERY->add_mfun(QUERY,model3d_export_audio,"int","exportAudioSamples");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"int","axis");
    QUERY->add_arg(QUERY,"float[]","samples");
    QUERY->add_mfun(QUERY,model3d_project_triangles,"int","projectTriangles");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"float","verticalSpan");
    QUERY->add_arg(QUERY,"int","rgb");
    QUERY->add_arg(QUERY,"float[]","triangles");
    QUERY->add_mfun(QUERY,model3d_render_orthographic_opaque,"int","renderOrthographic");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"float","verticalSpan");
    QUERY->add_arg(QUERY,"int","rgb");
    QUERY->add_arg(QUERY,"int","backgroundRGB");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,model3d_render_orthographic,"int","renderOrthographic");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"float","verticalSpan");
    QUERY->add_arg(QUERY,"int","rgb");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,model3d_create_heightmap,"int","createHeightmap");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"float[]","heights");
    QUERY->add_arg(QUERY,"float","spacing");
    QUERY->add_arg(QUERY,"float","heightScale");
    QUERY->add_mfun(QUERY,model3d_create_silhouette,"int","createSilhouette");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int[]","alpha");
    QUERY->add_arg(QUERY,"int","threshold");
    QUERY->add_arg(QUERY,"float","depth");
    QUERY->add_mfun(QUERY,model3d_create_mesh,"int","createMesh");
    QUERY->add_arg(QUERY,"float[]","xyz");
    QUERY->add_arg(QUERY,"int[]","triangles");
    QUERY->add_mfun(QUERY,model3d_create_boxes,"int","createBoxes");
    QUERY->add_arg(QUERY,"float[]","boxes");
    QUERY->add_mfun(QUERY,model3d_create_box,"int","createBox");
    QUERY->add_arg(QUERY,"float","width");
    QUERY->add_arg(QUERY,"float","height");
    QUERY->add_arg(QUERY,"float","depth");
    QUERY->add_mfun(QUERY,model3d_mesh_vertices,"int","meshVertices");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_mfun(QUERY,model3d_faces,"int","faces");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_mfun(QUERY,model3d_vertex,"float","vertexAt");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"int","axis");
    QUERY->add_mfun(QUERY,model3d_face_size,"int","faceSize");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"int","face");
    QUERY->add_mfun(QUERY,model3d_face_index,"int","faceIndex");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"int","face");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,model3d_set_vertex,"int","setVertex");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_arg(QUERY,"float","z");
    QUERY->add_mfun(QUERY,model3d_export_vertices,"int","exportVertices");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"float[]","xyz");
    QUERY->add_mfun(QUERY,model3d_export_triangles,"int","exportTriangles");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"int[]","triangles");
    QUERY->add_mfun(QUERY,model3d_add_mesh,"int","addMesh");
    QUERY->add_arg(QUERY,"float[]","xyz");
    QUERY->add_arg(QUERY,"int[]","triangles");
    QUERY->add_mfun(QUERY,model3d_set_mesh_vertex,"int","setVertex");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_arg(QUERY,"float","z");
    QUERY->add_mfun(QUERY,model3d_transform_mesh,"int","transformMesh");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"float[]","matrix");
    QUERY->add_mfun(QUERY,model3d_compute_normals,"int","computeNormals");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_mfun(QUERY,model3d_has_normals,"int","hasNormals");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_mfun(QUERY,model3d_export_normals,"int","exportNormals");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"float[]","xyz");
    QUERY->add_mfun(QUERY,model3d_set_uv,"int","setUV");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"float[]","uv");
    QUERY->add_mfun(QUERY,model3d_export_uv,"int","exportUV");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"float[]","uv");
    QUERY->add_mfun(QUERY,model3d_material,"int","addMaterial");
    QUERY->add_arg(QUERY,"string","name");
    QUERY->add_arg(QUERY,"float","red");
    QUERY->add_arg(QUERY,"float","green");
    QUERY->add_arg(QUERY,"float","blue");
    QUERY->add_arg(QUERY,"float","opacity");
    QUERY->add_mfun(QUERY,model3d_material_count,"int","materials");
    QUERY->add_mfun(QUERY,model3d_material_at,"int","materialAt");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_mfun(QUERY,model3d_export_material,"int","exportMaterial");
    QUERY->add_arg(QUERY,"int","materialIndex");
    QUERY->add_arg(QUERY,"float[]","properties");
    QUERY->add_mfun(QUERY,model3d_export_material_lighting,"int","exportMaterialLighting");
    QUERY->add_arg(QUERY,"int","materialIndex");
    QUERY->add_arg(QUERY,"float[]","properties");
    QUERY->add_mfun(QUERY,model3d_base_texture_count,"int","baseColorTextures");
    QUERY->add_arg(QUERY,"int","materialIndex");
    QUERY->add_mfun(QUERY,model3d_base_texture_path,"string","baseColorTexturePath");
    QUERY->add_arg(QUERY,"int","materialIndex");
    QUERY->add_arg(QUERY,"int","slot");
    QUERY->add_mfun(QUERY,model3d_embedded_texture_format,"string","embeddedBaseColorFormat");
    QUERY->add_arg(QUERY,"int","materialIndex");
    QUERY->add_arg(QUERY,"int","slot");
    QUERY->add_mfun(QUERY,model3d_export_embedded_texture,"int","exportEmbeddedBaseColor");
    QUERY->add_arg(QUERY,"int","materialIndex");
    QUERY->add_arg(QUERY,"int","slot");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_mfun(QUERY,model3d_assign_material,"int","assignMaterial");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"int","material");
    QUERY->add_mfun(QUERY,model3d_set_colors,"int","setColors");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"float[]","rgba");
    QUERY->add_mfun(QUERY,model3d_export_colors,"int","exportColors");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"float[]","rgba");
    QUERY->add_mfun(QUERY,model3d_set_skin,"int","setSkin");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"int","boneCount");
    QUERY->add_arg(QUERY,"float[]","weights");
    QUERY->add_mfun(QUERY,model3d_bones,"int","bones");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_mfun(QUERY,model3d_export_weights,"int","exportWeights");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"float[]","weights");
    QUERY->add_mfun(QUERY,model3d_node_count,"int","nodes");
    QUERY->add_mfun(QUERY,model3d_node_parent,"int","nodeParent");
    QUERY->add_arg(QUERY,"int","nodeIndex");
    QUERY->add_mfun(QUERY,model3d_node_mesh_count,"int","nodeMeshes");
    QUERY->add_arg(QUERY,"int","nodeIndex");
    QUERY->add_mfun(QUERY,model3d_node_mesh_at,"int","nodeMeshAt");
    QUERY->add_arg(QUERY,"int","nodeIndex");
    QUERY->add_arg(QUERY,"int","slot");
    QUERY->add_mfun(QUERY,model3d_node_local_matrix,"int","exportNodeLocalMatrix");
    QUERY->add_arg(QUERY,"int","nodeIndex");
    QUERY->add_arg(QUERY,"float[]","matrix");
    QUERY->add_mfun(QUERY,model3d_node_world_matrix,"int","exportNodeWorldMatrix");
    QUERY->add_arg(QUERY,"int","nodeIndex");
    QUERY->add_arg(QUERY,"float[]","matrix");
    QUERY->add_mfun(QUERY,model3d_export_node_trs,"int","exportNodeTRS");
    QUERY->add_arg(QUERY,"int","nodeIndex");
    QUERY->add_arg(QUERY,"float[]","values");
    QUERY->add_mfun(QUERY,model3d_bone_parent,"int","boneParent");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"int","bone");
    QUERY->add_mfun(QUERY,model3d_set_bone_parent,"int","setBoneParent");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"int","bone");
    QUERY->add_arg(QUERY,"int","parent");
    QUERY->add_mfun(QUERY,model3d_bone_pose,"int","setBonePose");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"int","bone");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_arg(QUERY,"float","z");
    QUERY->add_arg(QUERY,"float","rotationX");
    QUERY->add_arg(QUERY,"float","rotationY");
    QUERY->add_arg(QUERY,"float","rotationZ");
    QUERY->add_mfun(QUERY,model3d_reset_pose,"int","resetPose");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_mfun(QUERY,model3d_export_posed,"int","exportPosedVertices");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"float[]","xyz");
    QUERY->add_mfun(QUERY,model3d_export_posed_normals,"int","exportPosedNormals");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"float[]","xyz");
    QUERY->add_mfun(QUERY,model3d_export_posed_geometry,"int","exportPosedGeometry");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"float[]","xyz");
    QUERY->add_arg(QUERY,"float[]","normals");
    QUERY->add_mfun(QUERY,model3d_add_animation,"int","addAnimation");
    QUERY->add_arg(QUERY,"int","mesh");
    QUERY->add_arg(QUERY,"string","name");
    QUERY->add_arg(QUERY,"float[]","times");
    QUERY->add_arg(QUERY,"float[]","poses");
    QUERY->add_mfun(QUERY,model3d_animations,"int","animations");
    QUERY->add_mfun(QUERY,model3d_animation_duration,"float","animationDuration");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,model3d_blend_animations,"int","blendAnimations");
    QUERY->add_arg(QUERY,"int","clipA");
    QUERY->add_arg(QUERY,"float","firstSeconds");
    QUERY->add_arg(QUERY,"int","clipB");
    QUERY->add_arg(QUERY,"float","secondSeconds");
    QUERY->add_arg(QUERY,"float","weight");
    QUERY->add_arg(QUERY,"int","loop");
    QUERY->add_mfun(QUERY,model3d_play,"int","playAnimation");
    QUERY->add_arg(QUERY,"int","clip");
    QUERY->add_arg(QUERY,"int","loop");
    QUERY->add_mfun(QUERY,model3d_playback_speed,"int","playbackSpeed");
    QUERY->add_arg(QUERY,"float","speed");
    QUERY->add_mfun(QUERY,model3d_playback_pause,"int","pauseAnimation");
    QUERY->add_arg(QUERY,"int","paused");
    QUERY->add_mfun(QUERY,model3d_playback_seek,"int","seekAnimation");
    QUERY->add_arg(QUERY,"float","position");
    QUERY->add_mfun(QUERY,model3d_playback_fade,"int","fadeAnimation");
    QUERY->add_arg(QUERY,"int","clip");
    QUERY->add_arg(QUERY,"float","duration");
    QUERY->add_mfun(QUERY,model3d_playback_advance,"int","advanceAnimation");
    QUERY->add_arg(QUERY,"float","delta");
    QUERY->add_mfun(QUERY,model3d_playback_time,"float","playbackTime");
    QUERY->add_mfun(QUERY,model3d_playback_clip,"int","playbackClip");
    QUERY->add_mfun(QUERY,model3d_playback_ended,"int","playbackEnded");
    QUERY->add_mfun(QUERY,model3d_playback_stop,"void","stopAnimation");
    QUERY->add_mfun(QUERY,model3d_reset_animation,"int","resetAnimation");
    QUERY->add_mfun(QUERY,model3d_sample_animation,"int","sampleAnimation");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"float","seconds");
    QUERY->add_arg(QUERY,"int","loop");
    return QUERY->end_class(QUERY);
}
