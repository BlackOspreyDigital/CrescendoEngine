#pragma once 

#include <vector>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "scene/BaseEntity.hpp"

#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/base/math/soa_transform.h"

namespace Crescendo {

class RenderingServer;

struct Bone {
    std::string name;
    int parentIndex;
    glm::mat4 inverseBindMatrix;
};

struct Skeleton {
    std::vector<Bone> bones;
    glm::mat4 globalInverseTransform;
};

// Represents a timeline of keyframes for a single bone
struct AnimationChannel {
    std::vector<float> timestamps;
    std::vector<glm::vec3> positions;
    std::vector<glm::quat> rotations;
    std::vector<glm::vec3> scales;
};

struct AnimationClip {
    std::string name;
    float duration;
    float ticksPerSecond;
    // Maps a bone index to its animation track
    std::unordered_map<int, AnimationChannel> channels;
};

// The component attached to a CBaseEntity
class AnimatiorComponent : public Component {
public:
    Skeleton* skeleton = nullptr;
    AnimationClip* currentClip = nullptr;

    float currentTime = 0.0f;
    bool isPlaying = false;
    bool loop = true;

    // The calculated matrices ready to be uploaded to SSBO
    std::vector<glm::mat4> finalBoneMatrices;

    AnimatorComponent() = default; 
    ~AnimatorComponent() override = default;

    void Play(AnimationClip* clip, bool loopAnimation = true) {
        currentClip = clip;
        currentTime = 0.0f;
        loop = loopAnimation;
        isPlaying = true;
    }
};

class AnimationServer {
public:
    AnimationServer();
    ~AnimationServer();

    void Initialize();
    
    // Step the animation time and interpolate keyframe for all active ents
    void Update(float deltaTime);

    // Gather all finalBoneMatrices and submit to renderingserver
    void SyncWithRendering(RenderingServer* renderingServer);

    // Register ent animation component for processing
    void RegisterAnimator(AnimatorComponent* animator);
    void UnregisteredAnimator(AnimatorComponent* animator);

private:
    std::vector<AnimatorComponent*> activeAnimators;

    // Traverse skeleton hierarchy to compute global transforms
    void CalculateBoneTransform(AnimatorComponent* animator, int boneIndex, const glm::mat4& parentTransform);

    // Helper to interpolate between two keyframes based on current time
    glm::mat4 InterpolateChannel(const AnimationChannel& channel, float time);
};


}