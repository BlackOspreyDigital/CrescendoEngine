#pragma once

#include <enet/enet.h>
#include <glm/glm.hpp>
#include "scene/BaseEntity.hpp"
#include <string>

class CBaseEntity;

namespace Crescendo {

    enum class PacketType : uint8_t {
        TRANSFORM_UPDATE = 0,
        SPAWN_ENTITY = 1,
        DESPAWN_ENTITY = 2
    };

#pragma pack(push, 1)
    struct TransformPacket {
        PacketType type;
        uint32_t networkID;
        glm::vec3 position; // (Fixed 'positoin' typo)
        glm::vec3 rotation;
    }; // <-- You were missing this closing bracket!
#pragma pack(pop)

    class NetworkingServer {
    public:
        NetworkingServer();
        ~NetworkingServer();

        bool Initialize(bool asServer, int port = 7777, const std::string& address = "127.0.0.1");
        void Poll(std::vector<CBaseEntity*>& entities);
        void Shutdown();
        void BroadcastTransform(uint32_t netID, const glm::vec3& pos, const glm::vec3& rot);

        bool IsServer() const { return isServer; }
        bool IsConnected() const { return host != nullptr; }

    private:
        bool isServer;
        ENetHost* host;
        ENetPeer* serverPeer;
    };
}