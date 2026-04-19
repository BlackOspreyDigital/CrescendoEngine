#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#ifdef __EMSCRIPTEN__
    // RenderingServer.cpp is stripped from the Web build, so we MUST define stb_image here!
    #define STB_IMAGE_IMPLEMENTATION
#endif
// In Desktop builds, RenderingServer.cpp defines STB_IMAGE_IMPLEMENTATION to avoid duplicate symbols.

#include "AssetLoader.hpp"
#include "servers/physics/PhysicsServer.hpp"
#include "scene/components/TransformComponent.hpp"
#include "scene/components/MeshRendererComponent.hpp"
#include "tiny_gltf.h"
#include "deps/xatlas.h"
#include <iostream>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

// --- PLATFORM RENDERER INCLUDES ---
#ifdef __EMSCRIPTEN__
    #include "servers/rendering/Webgpu/WebGPURenderer.hpp"
#else
    #include "servers/rendering/RenderingServer.hpp"
#endif

namespace Crescendo {

    static std::string normalizePath(const std::string& path) {
        std::string s = path;
        for (char &c : s) if (c == '\\') c = '/';
        return s;
    }

    static std::string decodeUri(const std::string& uri) {
        std::string result;
        for (size_t i = 0; i < uri.length(); i++) {
            if (uri[i] == '%' && i + 2 < uri.length()) {
                std::string hex = uri.substr(i + 1, 2);
                char c = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
                result += c;
                i += 2;
            } else if (uri[i] == '+') result += ' ';
            else result += uri[i];
        }
        return result;
    }

    [[maybe_unused]] static void GenerateLightmapUVs(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
        xatlas::Atlas* atlas = xatlas::Create();

        xatlas::MeshDecl meshDecl;
        meshDecl.vertexCount = vertices.size();
        meshDecl.vertexPositionData = &vertices[0].pos;
        meshDecl.vertexPositionStride = sizeof(Vertex);

        meshDecl.indexCount = indices.size();
        meshDecl.indexData = indices.data();
        meshDecl.indexFormat = xatlas::IndexFormat::UInt32;

        xatlas::AddMeshError error = xatlas::AddMesh(atlas, meshDecl, 1);
        if (error != xatlas::AddMeshError::Success) {
            std::cerr <<"[Lightmapper] Error adding mesh to xatlas!" << std::endl;
            xatlas::Destroy(atlas);
            return;
        }

        xatlas::Generate(atlas);

        std::vector<Vertex> newVertices;
        std::vector<uint32_t> newIndices;

        const xatlas::Mesh& outMesh = atlas->meshes[0];
        newVertices.resize(outMesh.vertexCount);
        newIndices.resize(outMesh.indexCount);

        for (uint32_t i = 0; i < outMesh.indexCount; i++) {
            newIndices[i] = outMesh.indexArray[i];
        }

        for (uint32_t i = 0; i < outMesh.vertexCount; i++) {
            const xatlas::Vertex& v = outMesh.vertexArray[i];
            newVertices[i] = vertices[v.xref]; 
            newVertices[i].lightmapUV.x = v.uv[0] / (float)atlas->width;
            newVertices[i].lightmapUV.y = v.uv[1] / (float)atlas->height;
        }

        vertices = std::move(newVertices);
        indices = std::move(newIndices);

        std::cout << "[Lightmapper] Atlas generated. Size : " << atlas->width << "x" << atlas->height<< std::endl;
        xatlas::Destroy(atlas);
    }

    void AssetLoader::loadModel(IRenderer* renderer, const std::string& filePath, Scene* scene) {
        std::ifstream f(filePath.c_str());
        if (!f.good()) {
            std::cerr << "[Error] File not found: " << filePath << std::endl;
            return;
        }

        if (filePath.find(".glb") != std::string::npos || filePath.find(".gltf") != std::string::npos) {
            loadGLTF(renderer, filePath, scene); 
        } else if (filePath.find(".obj") != std::string::npos) {
            std::cout << "[Loader] OBJ loading not yet refactored." << std::endl;
        }
    }

    void AssetLoader::loadGLTF(IRenderer* renderer, const std::string& filePath, Scene* scene) {
        if (scene == nullptr) return;

        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err, warn;

        bool ret = (filePath.find(".glb") != std::string::npos) ? 
                   loader.LoadBinaryFromFile(&model, &err, &warn, filePath) : 
                   loader.LoadASCIIFromFile(&model, &err, &warn, filePath);

        if (!ret) { std::cerr << "[GLTF Error] " << err << std::endl; return; }

        std::string baseDir = "";
        size_t lastSlash = filePath.find_last_of("/\\");
        if (lastSlash != std::string::npos) baseDir = filePath.substr(0, lastSlash);
        baseDir = normalizePath(baseDir);

        RawMeshMap rawMeshes;

        for (size_t i = 0; i < model.meshes.size(); i++) {
            const auto& mesh = model.meshes[i];

            for (size_t j = 0; j < mesh.primitives.size(); j++) {
                const auto& primitive = mesh.primitives[j];
                std::vector<Vertex> vertices;
                std::vector<uint32_t> indices;

                auto getAttrData = [&](const std::string& name, int& stride) -> const uint8_t* {
                    auto it = primitive.attributes.find(name);
                    if (it == primitive.attributes.end()) return nullptr;
                    const auto& acc = model.accessors[it->second];
                    const auto& view = model.bufferViews[acc.bufferView];
                    stride = acc.ByteStride(view);
                    return &model.buffers[view.buffer].data[acc.byteOffset + view.byteOffset];
                };

                auto posIt = primitive.attributes.find("POSITION");
                if (posIt == primitive.attributes.end()) continue;
                int posCount = model.accessors[posIt->second].count;

                int posStride = 0, normStride = 0, texStride = 0;
                const uint8_t* posBase = getAttrData("POSITION", posStride);
                const uint8_t* normBase = getAttrData("NORMAL", normStride);
                const uint8_t* texBase = getAttrData("TEXCOORD_0", texStride);

                vertices.reserve(posCount);

                for (int v = 0; v < posCount; v++) {
                    Vertex vert{};
                    const float* p = reinterpret_cast<const float*>(posBase + (v * posStride));
                    vert.pos = { p[0], p[1], p[2] };

                    if (normBase) {
                        const float* n = reinterpret_cast<const float*>(normBase + (v * normStride));
                        vert.normal = { n[0], n[1], n[2] };
                    } else { vert.normal = { 0.0f, 0.0f, 1.0f }; }

                    if (texBase) {
                        const auto& acc = model.accessors[primitive.attributes.at("TEXCOORD_0")];
                        if (acc.componentType == 5126) {
                            const float* t = reinterpret_cast<const float*>(texBase + (v * texStride));
                            vert.texCoord = { t[0], t[1] };
                        }
                        else if (acc.componentType == 5123) {
                            const uint16_t* t = reinterpret_cast<const uint16_t*>(texBase + (v * texStride));
                            vert.texCoord = { t[0] / 65535.0f, t[1] / 65535.0f };
                        }
                    }

                    vert.color = { 1.0f, 1.0f, 1.0f };
                    vert.tangent = { 1.0f, 0.0f, 0.0f }; 
                    vert.bitangent = { 0.0f, 1.0f, 0.0f };
                    vertices.push_back(vert);
                }

                if (primitive.indices > -1) {
                    const auto& acc = model.accessors[primitive.indices];
                    const auto& view = model.bufferViews[acc.bufferView];
                    const uint8_t* idxData = &model.buffers[view.buffer].data[acc.byteOffset + view.byteOffset];
                    int idxStride = acc.ByteStride(view);

                    for (size_t k = 0; k < acc.count; k++) {
                        if (acc.componentType == 5125) indices.push_back(*(const uint32_t*)(idxData + k * idxStride));
                        else if (acc.componentType == 5123) indices.push_back(*(const uint16_t*)(idxData + k * idxStride));
                        else if (acc.componentType == 5121) indices.push_back(*(const uint8_t*)(idxData + k * idxStride));
                    }
                } else {
                    for (uint32_t k = 0; k < vertices.size(); k++) indices.push_back(k);
                }

                if (vertices.empty() || indices.empty()) continue;
                
                std::string meshKey = baseDir + "_mesh_" + std::to_string(i) + "_" + std::to_string(j); 
                rawMeshes[meshKey] = {vertices, indices};

#ifndef __EMSCRIPTEN__
                // --- VULKAN MESH BINDING ---
                auto* backend = static_cast<RenderingServer*>(renderer);
                MeshResource newMesh{};
                newMesh.name = meshKey;
                newMesh.indexCount = static_cast<uint32_t>(indices.size());
                newMesh.vertexBuffer = backend->createVertexBuffer(vertices);
                newMesh.indexBuffer = backend->createIndexBuffer(indices);

                size_t globalIndex = backend->meshes.size();
                backend->meshes.push_back(std::move(newMesh));
                backend->meshMap[meshKey] = globalIndex;
#else
                // --- WEBGPU MESH BINDING ---
                auto* backend = static_cast<WebGPURenderer*>(renderer);

                uint64_t vertexSize = vertices.size() * sizeof(Vertex);
                uint64_t indexSize = indices.size() * sizeof(uint32_t);

                // 1. Create and Write Vertex Buffer
                wgpu::BufferDescriptor vDesc{};
                vDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
                vDesc.size = vertexSize;
                wgpu::Buffer vBuffer = backend->device.CreateBuffer(&vDesc);
                backend->queue.WriteBuffer(vBuffer, 0, vertices.data(), vertexSize);

                // 2. Create and Write Index Buffer
                wgpu::BufferDescriptor iDesc{};
                iDesc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
                iDesc.size = indexSize;
                wgpu::Buffer iBuffer = backend->device.CreateBuffer(&iDesc);
                backend->queue.WriteBuffer(iBuffer, 0, indices.data(), indexSize);

                // 3. Store in the Renderer
                WebGPUMesh newMesh{};
                newMesh.name = meshKey;
                newMesh.vertexBuffer = vBuffer;
                newMesh.indexBuffer = iBuffer;
                newMesh.indexCount = static_cast<uint32_t>(indices.size());

                size_t globalIndex = backend->meshes.size();
                backend->meshes.push_back(std::move(newMesh));
                backend->meshMap[meshKey] = globalIndex;
#endif
            }
        }

        const auto& gltfScene = model.scenes[model.defaultScene > -1 ? model.defaultScene : 0];
        for (int nodeIdx : gltfScene.nodes) {
            processGLTFNode(renderer, model, model.nodes[nodeIdx], nullptr, baseDir, filePath, scene, glm::mat4(1.0f), rawMeshes);
        }
    }

    void AssetLoader::processGLTFNode(IRenderer* renderer, tinygltf::Model& model, tinygltf::Node& node, CBaseEntity* parent, const std::string& baseDir, const std::string& filePath, Scene* scene, glm::mat4 parentMatrix, RawMeshMap& rawMeshes) {
        if (!scene) return; 

        CBaseEntity* newEnt = scene->CreateEntity("prop_static"); 
        newEnt->targetName = node.name; 
        newEnt->textureID = 0; 
        newEnt->modelPath = filePath;

        newEnt->AddComponent<Crescendo::TransformComponent>();
        newEnt->AddComponent<Crescendo::MeshRendererComponent>();

        if (parent) {
            newEnt->moveParent = parent;
            parent->children.push_back(newEnt);
        }

        // --- 1. EXTRACT LOCAL MATRIX & CALCULATE WORLD MATRIX ---
        glm::vec3 localTrans(0.0f);
        glm::quat localRot = glm::identity<glm::quat>();
        glm::vec3 localScale(1.0f);
        glm::mat4 localMatrix(1.0f);

        if (node.matrix.size() == 16) {
            for (int i = 0; i < 16; ++i) {
                localMatrix[i / 4][i % 4] = static_cast<float>(node.matrix[i]);
            }
        } else {
            if (node.translation.size() == 3) localTrans = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
            if (node.rotation.size() == 4) localRot = glm::quat((float)node.rotation[3], (float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2]);
            if (node.scale.size() == 3) localScale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
            
            // Build local matrix from components
            glm::mat4 tMat = glm::translate(glm::mat4(1.0f), localTrans);
            glm::mat4 rMat = glm::mat4_cast(localRot);
            glm::mat4 sMat = glm::scale(glm::mat4(1.0f), localScale);
            localMatrix = tMat * rMat * sMat;
        }

        // Accumulate parent transforms to get Absolute World Matrix
        glm::mat4 worldMatrix = parentMatrix * localMatrix;

        // --- 2. DECOMPOSE WORLD MATRIX FOR ENGINE PROPERTIES ---
        glm::vec3 wScale;
        glm::quat wRot;
        glm::vec3 wTrans;
        glm::vec3 wSkew;
        glm::vec4 wPersp;
        glm::decompose(worldMatrix, wScale, wRot, wTrans, wSkew, wPersp);

        newEnt->origin = wTrans;
        newEnt->scale = wScale;
        newEnt->angles = glm::degrees(glm::eulerAngles(wRot)); 

        if (node.mesh > -1) {
            const tinygltf::Mesh& mesh = model.meshes[node.mesh];
            
            for (size_t i = 0; i < mesh.primitives.size(); i++) {
                const auto& primitive = mesh.primitives[i];
                CBaseEntity* targetEnt = (i == 0) ? newEnt : scene->CreateEntity("prop_submesh");

                if (i > 0) {
                    targetEnt->moveParent = newEnt;
                    newEnt->children.push_back(targetEnt);
                    // Local transform relative to parent is zeroed out
                    targetEnt->origin = glm::vec3(0.0f);
                    targetEnt->angles = glm::vec3(0.0f);
                    targetEnt->scale = glm::vec3(1.0f);
                }

                if (primitive.material >= 0) {
                    const tinygltf::Material& mat = model.materials[primitive.material];
                    
                    targetEnt->roughness = (float)mat.pbrMetallicRoughness.roughnessFactor;
                    targetEnt->metallic = (float)mat.pbrMetallicRoughness.metallicFactor;

                    if (mat.pbrMetallicRoughness.baseColorFactor.size() == 4) {
                        targetEnt->albedoColor = glm::vec3(
                            (float)mat.pbrMetallicRoughness.baseColorFactor[0],
                            (float)mat.pbrMetallicRoughness.baseColorFactor[1],
                            (float)mat.pbrMetallicRoughness.baseColorFactor[2]
                        );
                    }

                    int texIndex = mat.pbrMetallicRoughness.baseColorTexture.index;
                    if (texIndex >= 0) {
                        const tinygltf::Texture& tex = model.textures[texIndex];
                        const tinygltf::Image& img = model.images[tex.source];
                        
                        std::string texKey;
                        if (!img.uri.empty()) texKey = baseDir + "/" + decodeUri(img.uri);
                        else texKey = "EMBEDDED_" + std::to_string(tex.source) + "_" + node.name;
                        
#ifndef __EMSCRIPTEN__
                        // --- VULKAN TEXTURE BINDING ---
                        auto* backend = static_cast<RenderingServer*>(renderer);
                        if (backend->textureMap.find(texKey) != backend->textureMap.end()) {
                            targetEnt->textureID = backend->textureMap[texKey];
                        } else {
                            int newID = static_cast<int>(backend->textureMap.size()) + 1;
                            if (newID < backend->MAX_TEXTURES) {
                                TextureResource newTex;
                                bool success = false;
                                VkFormat format = VK_FORMAT_R8G8B8A8_UNORM; 

                                if (!img.image.empty()) {
                                    newTex.image = backend->UploadTexture((void*)img.image.data(), img.width, img.height, format);
                                    success = true;
                                } 
                                else if (!img.uri.empty()) {
                                    if (backend->createTextureImage(texKey, newTex.image)) success = true;
                                }

                                if (success) {
                                    backend->textureBank[newID] = std::move(newTex);
                                    backend->textureMap[texKey] = newID;
                                    backend->cache.textures[texKey] = newID; 
                                    targetEnt->textureID = newID;

                                    for (size_t frame = 0; frame < backend->descriptorSets.size(); frame++) {
                                        VkDescriptorImageInfo imageInfo{};
                                        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                                        imageInfo.imageView = backend->textureBank[newID].image.view;
                                        imageInfo.sampler = backend->textureSampler;

                                        VkWriteDescriptorSet descriptorWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                                        descriptorWrite.dstSet = backend->descriptorSets[frame]; 
                                        descriptorWrite.dstBinding = 0;
                                        descriptorWrite.dstArrayElement = newID; 
                                        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                                        descriptorWrite.descriptorCount = 1;
                                        descriptorWrite.pImageInfo = &imageInfo;
                                        
                                        vkUpdateDescriptorSets(backend->device, 1, &descriptorWrite, 0, nullptr);
                                    }
                                }
                            }
                        }
#else
                        // --- WEBGPU TEXTURE BINDING ---
                        auto* backend = static_cast<WebGPURenderer*>(renderer);
                        // TODO: Next step is wiring up WebGPU Texture uploads!
#endif
                    }
                } else { targetEnt->textureID = 0; }
                
                std::string meshKey = normalizePath(baseDir) + "_mesh_" + std::to_string(node.mesh) + "_" + std::to_string(i); 

                bool hasMeshReady = false;

#ifndef __EMSCRIPTEN__
                auto* backend = static_cast<RenderingServer*>(renderer);
                if (backend->meshMap.find(meshKey) != backend->meshMap.end()) {
                    targetEnt->modelIndex = backend->meshMap[meshKey];
                    hasMeshReady = true;
                }
#else
                auto* backend = static_cast<WebGPURenderer*>(renderer);
                targetEnt->modelIndex = 0; // Temporary bypass
                hasMeshReady = true;
#endif
                
                if (hasMeshReady && scene->physics && rawMeshes.find(meshKey) != rawMeshes.end()) {
                    scene->physics->CreateMeshCollider(
                        targetEnt->index, 
                        rawMeshes[meshKey].first, 
                        rawMeshes[meshKey].second, 
                        newEnt->origin, // <-- Forced absolute world origin for physics
                        newEnt->scale   // <-- Forced absolute world scale for physics
                    );
                }
            }
        }

        // --- 3. RECURSIVELY PASS WORLD MATRIX TO CHILDREN ---
        for (int childId : node.children) {
            processGLTFNode(renderer, model, model.nodes[childId], newEnt, baseDir, filePath, scene, worldMatrix, rawMeshes);
        }
    }
}