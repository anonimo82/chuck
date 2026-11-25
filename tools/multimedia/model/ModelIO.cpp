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
    std::unique_ptr<aiScene> generated;
    const aiScene *scene=nullptr;
    std::unordered_map<const aiNode*,aiMatrix4x4> importedPose;
    std::array<double,9> controls{{0,0,0,0,0,0,1,1,1}};
    void reset() { controls={{0,0,0,0,0,0,1,1,1}}; }
    bool read(const std::string &path) {
        auto candidate=std::make_unique<Assimp::Importer>();
        const auto *next=load(*candidate,path);
        if(!next) return false;
        importedPose.clear();
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
    if(auto *p=modelState(SELF,API)) { p->importedPose.clear(); p->scene=nullptr; p->importer.reset(); p->generated.reset(); p->reset(); }
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
CK_DLL_MFUN(model3d_base_texture_count) {
    RETURN->v_int=-1;
    auto *m=materialByIndex(modelState(SELF,API),GET_NEXT_INT(ARGS));
    if(!m) return;
    auto n=m->GetTextureCount(aiTextureType_BASE_COLOR);
    RETURN->v_int=n?n:m->GetTextureCount(aiTextureType_DIFFUSE);
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
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(model3d_reset_animation) {
    RETURN->v_int=0;
    auto *p=modelState(SELF,API);
    if(!p||!p->scene||p->generated) return;
    p->importedPose.clear(); RETURN->v_int=1;
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
    QUERY->add_mfun(QUERY,model3d_create_mesh,"int","createMesh");
    QUERY->add_arg(QUERY,"float[]","xyz");
    QUERY->add_arg(QUERY,"int[]","triangles");
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
    QUERY->add_mfun(QUERY,model3d_base_texture_count,"int","baseColorTextures");
    QUERY->add_arg(QUERY,"int","materialIndex");
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
    QUERY->add_mfun(QUERY,model3d_reset_animation,"int","resetAnimation");
    QUERY->add_mfun(QUERY,model3d_sample_animation,"int","sampleAnimation");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"float","seconds");
    QUERY->add_arg(QUERY,"int","loop");
    return QUERY->end_class(QUERY);
}
