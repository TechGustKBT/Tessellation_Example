
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include <vector>

namespace simulator {

    struct Vertex {
        glm::vec3 position, normal;
        glm::vec2 uv;
    };

    struct MeshRange {
        uint32_t firstIndex = 0, count = 0;
    };

    struct Geometry {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        MeshRange terrain, cube, sphere;
        Geometry();
    };

    struct Crater {
        glm::vec2 center;
        float radius, depth;
    };

    struct Shell {
        glm::vec3 start{}, velocity{};
        float age = 0.f;
        int kind = 0;
        uint32_t id = 0;
    };

    struct ShotTrail {
        glm::vec3 start{}, velocity{};
        float flightTime = 0.f, fade = 0.f;
        uint32_t shellId = 0;
        bool finished = false;
    };

    struct Burst {
        glm::vec3 center;
        float age = 0;
    };

    struct DrawObject {
        MeshRange mesh;
        glm::mat4 model{ 1 };
        glm::vec4 tint{ 1 };
        float displacement = 0, material = 1;
        bool transparent = false;
    };

    class Simulation {
    public:
        static constexpr int textureSize = 256;
        static constexpr float terrainSize = 32.f, heightScale = 8.f;
        glm::vec3 sightDirection() const;

        Geometry geometry;
        std::vector<Crater> craters;
        std::vector<Shell> shells;
        std::vector<ShotTrail> shotTrails;
        std::vector<Burst> bursts;
        std::vector<uint8_t> heights, albedo;
        std::vector<float> trackMarks, trackRidges;

        glm::vec2 tankPosition{ -8.f, -5.f };
        float tankYaw = 0.f;
        float turretYaw = 0.f, turretPitch = 0.f;

        bool aiming = false;
        bool cinematicArmed = false;
        bool cinematicActive = false;
        bool queuedCinematic = false;
        bool firstPerson = false;
        bool targetLocked = false;
        float sightYaw = 0.f;
        float sightPitch = 0.f;
        float trackLeftMotion = 0.f;
        float trackRightMotion = 0.f;

        glm::vec3 queuedTarget{}, cinematicEye{}, cinematicLook{};
        int queuedWeapon = 0;

        uint32_t cinematicShellId = 0, nextShellId = 1;
        float cinematicHold = 0.f;

        bool tracksPending = false;
        float trackTimer = 0.f;

        float time = 0, maxTess = 16;
        float cameraYaw = .15f, cameraPitch = .85f, cameraDistance = 35.f;
        glm::vec3 focus{ 0, 0, 0 };

        bool dirty = true, wireframe = false, normals = false;
        bool adaptive = true, paused = false;
        int weapon = 0;

        Simulation();

        float height(glm::vec2 xz) const;
        glm::vec3 cameraPosition() const;
        glm::mat4 viewProjection(float aspect) const;
        glm::vec3 pick(double mx, double my, int width, int height) const;

        void regenerate();
        void impact(glm::vec3 target);
        void fire(glm::vec3 target);
        void followTank(float dt);
        void recenterCamera();
        void tick(float dt);
        void moveTank(float drive, float steer, float dt, bool fast = false);
        void reset();

        std::vector<DrawObject> objects() const;

    private:
        void paintTrack(glm::vec2 point, float radius, bool turning);
    };

}
