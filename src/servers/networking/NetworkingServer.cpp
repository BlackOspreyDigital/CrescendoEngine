#include <iostream>
#include "servers/networking/NetworkingServer.hpp"
#include <enet/enet.h>

namespace Crescendo {

    NetworkingServer::NetworkingServer() : isServer(false), host(nullptr), serverPeer(nullptr) {}

    NetworkingServer::~NetworkingServer() {
        Shutdown();
    }

    bool NetworkingServer::Initialize(bool asServer, int port, const std::string& addressStr) {
        if (enet_initialize() != 0) {
            std::cerr << "[Network] An error occurred while initializing ENet." << std::endl;
            return false;
        }

        this->isServer = asServer;

        if (isServer) {
            ENetAddress address;
            address.host = ENET_HOST_ANY;
            address.port = port;

            host = enet_host_create(&address, 32, 2, 0, 0);
            if (host == nullptr) {
                std::cerr << "[Network] Failed to create ENet server host." << std::endl;
                return false;
            }
            std::cout << "[Network] Server started on port " << port << std::endl;
        } else {
            host = enet_host_create(nullptr, 1, 2, 0, 0);
            if (host == nullptr) {
                std::cerr << "[Network] Failed to create ENet client host." << std::endl;
                return false;
            }

            ENetAddress address;
            enet_address_set_host(&address, addressStr.c_str());
            address.port = port;

            serverPeer = enet_host_connect(host, &address, 2, 0);
            if (serverPeer == nullptr) {
                std::cerr << "[Network] No available peers for initiating connection." << std::endl;
                return false;
            }
            std::cout << "[Network] Client attempting to connect to " << addressStr << ":" << port << std::endl;
        }

        return true;
    }

    void NetworkingServer::Poll(std::vector<CBaseEntity*>& entities) {
        if (!host) return;

        ENetEvent event;
        while (enet_host_service(host, &event, 0) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT:
                    std::cout << "[Network] A new client connected!" << std::endl;
                    break;
                case ENET_EVENT_TYPE_RECEIVE:
                    if (event.packet->dataLength == sizeof(TransformPacket)) {
                        TransformPacket* packet = (TransformPacket*)event.packet->data;
                        
                        // Find the entity with the matching Network ID and apply the transform!
                        for (auto* ent : entities) {
                            if (ent->syncTransform && ent->networkID == packet->networkID) {
                                ent->origin = packet->position;
                                // Convert incoming radians back to degrees for the engine/inspector
                                ent->angles = glm::degrees(packet->rotation); 
                                break;
                            }
                        }
                    }
                    enet_packet_destroy(event.packet);
                    break;
                case ENET_EVENT_TYPE_DISCONNECT:
                    std::cout << "[Network] Peer disconnected." << std::endl;
                    event.peer->data = nullptr;
                    break;
                case ENET_EVENT_TYPE_NONE:
                    break;
            }
        }
    }

    void NetworkingServer::BroadcastTransform(uint32_t netID, const glm::vec3& pos, const glm::vec3& rot) {
        if (!host) return;

        TransformPacket data;
        data.type = PacketType::TRANSFORM_UPDATE;
        data.networkID = netID;
        data.position = pos;
        data.rotation = rot;

        ENetPacket* packet = enet_packet_create(&data, sizeof(TransformPacket), ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT);
        
        if (isServer) {
            enet_host_broadcast(host, 0, packet);
        } else if (serverPeer) {
            enet_peer_send(serverPeer, 0, packet);
        }
    }

    void NetworkingServer::Shutdown() {
        if (host) {
            enet_host_destroy(host);
            host = nullptr;
        }
        enet_deinitialize();
        std::cout << "[Network] Server shut down." << std::endl;
    }
}