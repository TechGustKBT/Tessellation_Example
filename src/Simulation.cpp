
#include "Simulation.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <cstring>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/quaternion.hpp>
#include "EmbeddedResources.hpp"
#include <sstream>

namespace simulator {


    static constexpr float METROS_POR_UNIDADE = 2.5f;
    static constexpr float VELOCIDADE_120MM = 1800.f / METROS_POR_UNIDADE;
    static constexpr float VELOCIDADE_ARTILHARIA = 850.f / METROS_POR_UNIDADE;
    static constexpr float GRAVIDADE = 9.81f / METROS_POR_UNIDADE;
    static constexpr float COMPRIMENTO_CANO = 2.35f;

    static float velocidadeDaArma(int tipo) {
        return tipo == 0 ? VELOCIDADE_120MM : VELOCIDADE_ARTILHARIA;
    }

    static glm::vec3 pontoBalistico(glm::vec3 origem, glm::vec3 velocidade, float segundos) {
        return origem + velocidade * segundos
            + glm::vec3(0.f, -0.5f * GRAVIDADE * segundos * segundos, 0.f);
    }


    static float terrainHash(int x, int z, uint32_t seed) {
        uint32_t h = uint32_t(x) * 374761393u
            ^ uint32_t(z) * 668265263u
            ^ seed * 1442695041u;

        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;

        return float(h & 0x00FFFFFFu) / 16777215.f;
    }

    static float terrainNoise(glm::vec2 p, uint32_t seed) {
        int x = int(std::floor(p.x));
        int z = int(std::floor(p.y));

        glm::vec2 f = glm::fract(p);
        f = f * f * (3.f - 2.f * f);

        float a = terrainHash(x, z, seed);
        float b = terrainHash(x + 1, z, seed);
        float c = terrainHash(x, z + 1, seed);
        float d = terrainHash(x + 1, z + 1, seed);

        return glm::mix(
            glm::mix(a, b, f.x),
            glm::mix(c, d, f.x),
            f.y
        ) * 2.f - 1.f;
    }

    static float proceduralHeight(glm::vec2 p, uint32_t seed) {
        float relevo = terrainNoise(p * .018f, seed);
        float colinas = terrainNoise(p * .065f, seed + 19u);
        float detalhes = terrainNoise(p * .22f, seed + 47u);
        float montanhas = terrainNoise(p * .035f, seed + 71u);

        float altura = relevo * 3.5f
            + colinas * 1.8f
            + detalhes * .4f
            + std::pow(std::max(0.f, montanhas + .1f), 2.f) * 8.f;

        // Deixa a região inicial mais plana para o tanque.
        float distancia = glm::length(p - glm::vec2(-8.f, -5.f));
        float plano = 1.f - glm::smoothstep(4.f, 12.f, distancia);

        return glm::mix(altura, altura * .15f, plano);
    }



    Geometry::Geometry() {
        auto begin = [&]() { return uint32_t(indices.size()); };

        terrain.firstIndex = begin();
        constexpr int cells = 64;

        for (int z = 0; z <= cells; z++)
            for (int x = 0; x <= cells; x++) {
                glm::vec2 uv{ float(x) / cells, float(z) / cells };
                vertices.push_back({
                    { (uv.x - .5f) * Simulation::terrainSize, 0,
                      (uv.y - .5f) * Simulation::terrainSize },
                    { 0, 1, 0 }, uv
                });
            }

        for (int z = 0; z < cells; z++)
            for (int x = 0; x < cells; x++) {
                uint32_t a = z * (cells + 1) + x, b = a + 1, c = a + cells + 1, d = c + 1;
                indices.insert(indices.end(), { a, c, b, b, c, d });
            }

        terrain.count = begin() - terrain.firstIndex;
        cube.firstIndex = begin();

        auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
            glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
            uint32_t first = static_cast<uint32_t>(vertices.size());
            vertices.insert(vertices.end(), { { a, n,{ 0, 0 } },{ b, n,{ 1, 0 } },{ c, n,{ 1, 1 } },{ d, n,{ 0, 1 } } });
            indices.insert(indices.end(), { first, first + 1, first + 2, first, first + 2, first + 3 });
            };

        quad({ -.5f,-.5f,.5f }, { .5f,-.5f,.5f }, { .5f,.5f,.5f }, { -.5f,.5f,.5f });
        quad({ .5f,-.5f,-.5f }, { -.5f,-.5f,-.5f }, { -.5f,.5f,-.5f }, { .5f,.5f,-.5f });
        quad({ .5f,-.5f,.5f }, { .5f,-.5f,-.5f }, { .5f,.5f,-.5f }, { .5f,.5f,.5f });
        quad({ -.5f,-.5f,-.5f }, { -.5f,-.5f,.5f }, { -.5f,.5f,.5f }, { -.5f,.5f,-.5f });
        quad({ -.5f,.5f,.5f }, { .5f,.5f,.5f }, { .5f,.5f,-.5f }, { -.5f,.5f,-.5f });
        quad({ -.5f,-.5f,-.5f }, { .5f,-.5f,-.5f }, { .5f,-.5f,.5f }, { -.5f,-.5f,.5f });

        cube.count = begin() - cube.firstIndex;
        sphere.firstIndex = begin();

        uint32_t first = static_cast<uint32_t>(vertices.size());
        constexpr int rings = 10, sectors = 16;

        for (int y = 0; y <= rings; y++)
            for (int x = 0; x <= sectors; x++) {
                float a = float(x) / sectors * glm::two_pi<float>();
                float b = float(y) / rings * glm::pi<float>();
                glm::vec3 n{ std::sin(b) * std::cos(a), std::cos(b), std::sin(b) * std::sin(a) };
                vertices.push_back({ n, n,{ float(x) / sectors, float(y) / rings } });
            }

        for (int y = 0; y < rings; y++)
            for (int x = 0; x < sectors; x++) {
                uint32_t a = first + y * (sectors + 1) + x, b = a + 1, c = a + sectors + 1, d = c + 1;
                if (y > 0) indices.insert(indices.end(), { a, b, c });
                if (y < rings - 1) indices.insert(indices.end(), { b, d, c });
            }

        sphere.count = begin() - sphere.firstIndex;
    }

    Simulation::Simulation() {
        trackMarks.assign(textureSize * textureSize, 0.f);
        trackRidges.assign(textureSize * textureSize, 0.f);

        regenerate();
        recenterCamera();
        followTank(0.f);
    }

    // Geometria ja convertida do GLB original do usuario: nenhum CMake ou biblioteca extra.
    void Simulation::loadAbrams(const std::string& path) {
        (void)path;

        auto resource = getEmbeddedResource(IDR_ABRAMS_MODEL);

        std::string binary(
            reinterpret_cast<const char*>(resource.data),
            resource.size
        );

        std::istringstream f(binary, std::ios::in | std::ios::binary);

        char magic[4] = {};
        uint32_t version = 0, count = 0;
        float header[9] = {};
        f.read(magic, 4);
        f.read(reinterpret_cast<char*>(&version), 4);
        f.read(reinterpret_cast<char*>(&count), 4);
        f.read(reinterpret_cast<char*>(header), sizeof(header));
        if (std::memcmp(magic, "TASM", 4) != 0 || version != 1 || count > 256)
            throw std::runtime_error("Formato Abrams_Model.tasm invalido.");

        abramsCenter = {header[0], 0.f, header[2]};
        abramsBottom = header[1];
        turretPivot = {header[3], header[4], header[5]};
        gunPivot = {header[6], header[7], header[8]};

        struct DiskPart {
            char name[64];
            uint32_t type, vertexCount, indexCount;
            float pivot[3];
        };
        static_assert(sizeof(DiskPart) == 88, "Formato de peca inesperado.");

        geometry.parts.clear();
        for (uint32_t i = 0; i < count; ++i) {
            DiskPart src{};
            f.read(reinterpret_cast<char*>(&src), sizeof(src));
            if (!f || !src.vertexCount || !src.indexCount ||
                src.vertexCount > 1000000 || src.indexCount > 3000000)
                throw std::runtime_error("Peca do Abrams invalida.");

            uint32_t vertexBase = static_cast<uint32_t>(geometry.vertices.size());
            uint32_t firstIndex = static_cast<uint32_t>(geometry.indices.size());
            geometry.vertices.resize(geometry.vertices.size() + src.vertexCount);
            f.read(reinterpret_cast<char*>(geometry.vertices.data() + vertexBase),
                   static_cast<std::streamsize>(src.vertexCount) * sizeof(Vertex));

            std::vector<uint32_t> localIndices(src.indexCount);
            f.read(reinterpret_cast<char*>(localIndices.data()),
                   static_cast<std::streamsize>(src.indexCount) * sizeof(uint32_t));
            if (!f) throw std::runtime_error("Dados incompletos em Abrams_Model.tasm.");
            for (uint32_t index : localIndices)
                geometry.indices.push_back(vertexBase + index);

            ModelPart part;
            part.name = src.name;
            part.mesh = {firstIndex, src.indexCount};
            part.type = src.type;
            part.pivot = {src.pivot[0], src.pivot[1], src.pivot[2]};
            geometry.parts.push_back(std::move(part));
        }
        hasAbrams = true;
        std::cout << "Abrams carregado: " << geometry.parts.size() << " pecas\n";
    }

    void Simulation::regenerate() {
        heights.resize(textureSize * textureSize * 4);
        albedo.resize(heights.size());


        if (terrainBase.size() != textureSize * textureSize) {
            terrainBase.resize(textureSize * textureSize);

            for (int z = 0; z < textureSize; ++z) {
                for (int x = 0; x < textureSize; ++x) {
                    glm::vec2 p{
                        ((x + .5f) / textureSize - .5f) * terrainSize,
                        ((z + .5f) / textureSize - .5f) * terrainSize
                    };

                    terrainBase[z * textureSize + x] =
                        proceduralHeight(p, terrainSeed);
                }
            }
        }


        for (int z = 0; z < textureSize; z++)
            for (int x = 0; x < textureSize; x++) {
                glm::vec2 p{
                    ((x + .5f) / textureSize - .5f) * terrainSize,
                    ((z + .5f) / textureSize - .5f) * terrainSize
                };

                float h = terrainBase[z * textureSize + x];
                float soil = 0.f;

                for (const auto& c : craters) {
                    float q = glm::length(p - c.center) / c.radius;
                    if (q < 1) {
                        float bowl = 1 - q * q;
                        h -= c.depth * bowl * bowl;
                        soil = std::max(soil, 1 - q * .4f);
                    }
                    else if (q < 1.3f) {
                        h += c.depth * .15f * std::sin((q - 1) / .3f * glm::pi<float>());
                        soil = std::max(soil, (1.3f - q) * 2);
                    }
                }

                int index = z * textureSize + x;

                float track = trackMarks[index];
                float ridge = trackRidges[index];

                // Afunda o terreno por onde a esteira passou.
                h -= track * .28f;

                // Acumula terra nas laterais das marcas.
                h += ridge * .075f;

                // Escurece o solo revolvido.
                soil = std::max(soil, glm::clamp(track * .8f + ridge * .45f, 0.f, 1.f));
                h = glm::clamp(h, -heightScale * .49f, heightScale * .49f);

                float grain = .94f + .06f * std::sin(x * 17.13f + z * 9.27f);

                glm::vec3 vegetacao = glm::mix(
                    glm::vec3(.30f, .39f, .22f),
                    glm::vec3(.43f, .43f, .40f),
                    glm::smoothstep(2.5f, 7.f, h)
                );

                glm::vec3 color = glm::mix(
                    vegetacao,
                    glm::vec3(.31f, .23f, .15f),
                    glm::clamp(soil, 0.f, 1.f)
                ) * grain;


                size_t i = (z * textureSize + x) * 4;
                uint8_t value = uint8_t(glm::clamp((h / heightScale + .5f) * 255, 0.f, 255.f));

                heights[i] = heights[i + 1] = heights[i + 2] = value;
                heights[i + 3] = 255;

                for (int k = 0; k < 3; k++)
                    albedo[i + k] = uint8_t(glm::clamp(color[k] * 255, 0.f, 255.f));
                albedo[i + 3] = 255;
            }

        dirty = true;
    }

    float Simulation::height(glm::vec2 p) const {
        glm::vec2 texel = (p / terrainSize + .5f) * float(textureSize) - .5f;
        int x = int(std::floor(texel.x)), z = int(std::floor(texel.y));
        float u = texel.x - x, v = texel.y - z;

        auto sample = [&](int a, int b) {
            a = std::clamp(a, 0, textureSize - 1);
            b = std::clamp(b, 0, textureSize - 1);
            return heights[(b * textureSize + a) * 4] / 255.f;
            };

        float h = glm::mix(
            glm::mix(sample(x, z), sample(x + 1, z), u),
            glm::mix(sample(x, z + 1), sample(x + 1, z + 1), u), v
        );

        return (h - .5f) * heightScale;
    }

    glm::mat4 Simulation::tankMatrix() const {
        const float comprimento = 2.0f;
        const float largura = 0.90f;

        glm::vec2 frente{
            std::cos(tankYaw),
            -std::sin(tankYaw)
        };

        glm::vec2 lado{
            std::sin(tankYaw),
            std::cos(tankYaw)
        };

        auto solo = [&](float x, float z) {
            return height(tankPosition + frente * x + lado * z);
            };

        // Altura do terreno nos quatro pontos das esteiras.
        float frenteEsq = solo(comprimento, -largura);
        float frenteDir = solo(comprimento, largura);
        float trasEsq = solo(-comprimento, -largura);
        float trasDir = solo(-comprimento, largura);

        float alturaFrente = (frenteEsq + frenteDir) * .5f;
        float alturaTras = (trasEsq + trasDir) * .5f;
        float alturaEsq = (frenteEsq + trasEsq) * .5f;
        float alturaDir = (frenteDir + trasDir) * .5f;

        // Inclinação longitudinal e lateral.
        glm::vec3 eixoFrente = glm::normalize(glm::vec3(
            frente.x,
            (alturaFrente - alturaTras) / (comprimento * 2.f),
            frente.y
        ));

        glm::vec3 eixoLado = glm::normalize(glm::vec3(
            lado.x,
            (alturaDir - alturaEsq) / (largura * 2.f),
            lado.y
        ));

        glm::vec3 cima = glm::normalize(
            glm::cross(eixoLado, eixoFrente)
        );

        eixoLado = glm::normalize(glm::cross(eixoFrente, cima));

        glm::mat4 rotacao(1.f);
        rotacao[0] = glm::vec4(eixoFrente, 0.f);
        rotacao[1] = glm::vec4(cima, 0.f);
        rotacao[2] = glm::vec4(eixoLado, 0.f);

        // Impede que algum ponto das esteiras atravesse o solo.
        float altura = height(tankPosition);

        auto contato = [&](float x, float z, float alturaSolo) {
            float deslocamentoY = eixoFrente.y * x + eixoLado.y * z;
            altura = std::max(altura, alturaSolo - deslocamentoY);
            };

        contato(comprimento, -largura, frenteEsq);
        contato(comprimento, largura, frenteDir);
        contato(-comprimento, -largura, trasEsq);
        contato(-comprimento, largura, trasDir);

        return glm::translate(
            glm::mat4(1.f),
            glm::vec3(tankPosition.x, altura + .06f, tankPosition.y)
        ) * rotacao *
            glm::scale(glm::mat4(1.f), glm::vec3(.56f)) *
            glm::translate(
                glm::mat4(1.f),
                glm::vec3(-abramsCenter.x, -abramsBottom, -abramsCenter.z)
            );
    }


    void Simulation::followTank(float dt) {
        glm::vec3 target{ tankPosition.x, height(tankPosition) + 1.35f, tankPosition.y };
        focus = dt <= 0.f ? target : glm::mix(focus, target, 1.f - std::exp(-dt * 9.f));
    }

    void Simulation::recenterCamera() {
        cameraYaw = tankYaw - glm::half_pi<float>();
        cameraPitch = .42f;
        cameraDistance = 10.f;
    }


    glm::vec3 Simulation::sightDirection() const {
        return {
            std::cos(sightYaw) * std::cos(sightPitch),
            std::sin(sightPitch),
            -std::sin(sightYaw) * std::cos(sightPitch)
        };
    }

    glm::vec3 Simulation::cameraPosition() const {
        if (cinematicActive)
            return cinematicEye;

        if (firstPerson)
            return { tankPosition.x, height(tankPosition) + 1.4f, tankPosition.y };

        return focus + cameraDistance * glm::vec3(
            std::sin(cameraYaw) * std::cos(cameraPitch),
            std::sin(cameraPitch),
            std::cos(cameraYaw) * std::cos(cameraPitch)
        );
    }


    glm::mat4 Simulation::viewProjection(float aspect) const {
        glm::mat4 projection = glm::perspective(
            glm::radians(45.f), std::max(.1f, aspect), .1f, terrainSize * 3.f
        );

        projection[1][1] *= -1.f;

        glm::vec3 eye = cameraPosition();

        glm::vec3 look = cinematicActive ? cinematicLook :
            (firstPerson ? eye + sightDirection() : focus);

        return projection * glm::lookAt(eye, look, glm::vec3(0, 1, 0));
    }



    glm::vec3 Simulation::pick(double mx, double my, int width, int screenHeight) const {
        glm::mat4 inv = glm::inverse(viewProjection(float(width) / std::max(1, screenHeight)));
        glm::vec4 a = inv * glm::vec4(2 * mx / std::max(1, width) - 1, 2 * my / std::max(1, screenHeight) - 1, 0, 1);
        glm::vec4 b = inv * glm::vec4(2 * mx / std::max(1, width) - 1, 2 * my / std::max(1, screenHeight) - 1, 1, 1);
        glm::vec3 start = glm::vec3(a) / a.w;
        glm::vec3 dir = glm::normalize(glm::vec3(b) / b.w - start);

        for (float t = .2f; t < terrainSize * 2.5f; t += .2f) {
            glm::vec3 p = start + dir * t;
            if (std::abs(p.x) > terrainSize * .5f - .3f ||
                std::abs(p.z) > terrainSize * .5f - .3f) continue;

            if (p.y <= height({ p.x, p.z })) {
                float lo = t - .2f, hi = t;
                for (int n = 0; n < 12; n++) {
                    float mid = (lo + hi) / 2;
                    glm::vec3 q = start + dir * mid;
                    if (q.y > height({ q.x,q.z })) lo = mid;
                    else hi = mid;
                }
                return start + dir * ((lo + hi) / 2);
            }
        }


        if (firstPerson) {
            if (hasAbrams) {
                glm::vec3 local = turretPivot + glm::vec3(
                    -1.35f,  // Um pouco atras da base do canhao
                    0.40f,  // Altura da camera
                    -0.75f   // Deslocamento para a esquerda
                );

                glm::mat4 giroTorre =
                    glm::translate(glm::mat4(1.f), turretPivot) *
                    glm::rotate(glm::mat4(1.f),
                        turretYaw - tankYaw, glm::vec3(0, 1, 0)) *
                    glm::translate(glm::mat4(1.f), -turretPivot);

                return glm::vec3(
                    tankMatrix() * giroTorre * glm::vec4(local, 1.f)
                );
            }

            return {
                tankPosition.x,
                height(tankPosition) + 1.4f,
                tankPosition.y
            };
        }


        return { focus.x, height({ focus.x, focus.z }), focus.z };
    }

    void Simulation::impact(glm::vec3 target) {
        craters.push_back({
            { target.x, target.z },
            weapon == 0 ? 1.6f : 2.7f,
            weapon == 0 ? .75f : 1.4f
            });

        if (craters.size() > 64) craters.erase(craters.begin());
        regenerate();
        bursts.push_back({ target, 0 });
    }

    void Simulation::fire(glm::vec3 target) {
        if (paused || aiming || cinematicActive || shells.size() >= 12) return;

        queuedTarget = target;
        queuedWeapon = weapon;
        queuedCinematic = cinematicArmed;
        cinematicArmed = false;
        aiming = true;
    }

    void Simulation::paintTrack(glm::vec2 point, float radius, bool turning) {
        glm::vec2 pixel = (point / terrainSize + .5f) * float(textureSize) - .5f;
        float r = radius * textureSize / terrainSize;

        int x0 = std::max(0, int(std::floor(pixel.x - r * 1.4f)));
        int x1 = std::min(textureSize - 1, int(std::ceil(pixel.x + r * 1.4f)));
        int z0 = std::max(0, int(std::floor(pixel.y - r * 1.4f)));
        int z1 = std::min(textureSize - 1, int(std::ceil(pixel.y + r * 1.4f)));

        for (int z = z0; z <= z1; z++) {
            for (int x = x0; x <= x1; x++) {
                float d = glm::length(glm::vec2(float(x), float(z)) - pixel) / r;
                int index = z * textureSize + x;

                // Sulco principal da esteira.
                if (d < 1.f) {
                    float contato = 1.f - d * d;
                    float intensidade = turning ? 1.25f : 1.f;

                    trackMarks[index] = std::max(
                        trackMarks[index],
                        contato * intensidade
                    );

                    // Ao girar, a esteira continua revolvendo o mesmo solo.
                    if (turning) {
                        trackMarks[index] = std::min(
                            1.7f,
                            trackMarks[index] + contato * .008f
                        );
                    }
                }

                // Terra acumulada nas laterais da esteira.
                float borda = glm::clamp(
                    1.f - std::abs(d - 1.07f) / .28f,
                    0.f,
                    1.f
                );

                trackRidges[index] = std::max(
                    trackRidges[index],
                    borda * (turning ? 1.25f : .75f)
                );
            }
        }
    }



    void Simulation::moveTank(float drive, float steer, float dt, bool fast) {
        if (paused) return;

        dt = glm::clamp(dt, 0.f, .05f);

        glm::vec2 previous = tankPosition;
        float previousYaw = tankYaw;

        tankYaw += steer * dt * 1.7f;
        float velocidade = drive * (fast ? 6.f : 3.5f);
        float giro = steer * 1.7f;

        trackLeftMotion += (velocidade - giro) * dt;
        trackRightMotion += (velocidade + giro) * dt;

        if (!aiming)
            turretYaw += tankYaw - previousYaw;

        glm::vec2 forward{
            std::cos(tankYaw),
            -std::sin(tankYaw)
        };

        tankPosition += forward * (drive * dt * (fast ? 6.f : 3.5f));
        float limite = terrainSize * .5f - 3.f;
        tankPosition = glm::clamp(tankPosition, glm::vec2(-limite), glm::vec2(limite));

        float distance = glm::length(tankPosition - previous);
        float rotation = std::abs(tankYaw - previousYaw);

        // Agora detecta tambem o giro parado.
        if (distance < .0001f && rotation < .0001f)
            return;

        bool turning = std::abs(steer) > .01f;

        // Mais amostras para evitar falhas nas curvas.
        int steps = std::max({
            1,
            int(std::ceil(distance / .07f)),
            int(std::ceil(rotation / .025f))
            });

        for (int i = 0; i <= steps; i++) {
            float t = float(i) / steps;

            float angle = glm::mix(previousYaw, tankYaw, t);
            glm::vec2 center = glm::mix(previous, tankPosition, t);

            glm::vec2 frente{
                std::cos(angle),
                -std::sin(angle)
            };

            glm::vec2 lateral{
                std::sin(angle),
                std::cos(angle)
            };

            // Percorre todo o comprimento das duas esteiras.
            for (float comprimento = -2.05f; comprimento <= 2.05f; comprimento += .16f) {
                for (float lado : {-1.f, 1.f}) {
                    glm::vec2 contato =
                        center +
                        frente * comprimento +
                        lateral * lado;

                    // Curvas provocam sulcos mais largos.
                    float largura = turning ? .42f : .26f;

                    paintTrack(contato, largura, turning);
                }
            }
        }

        // Atualiza o mapa de alturas e a textura do terreno.
        tracksPending = true;
    }



    void Simulation::tick(float dt) {
        if (paused) return;

        dt = glm::clamp(dt, 0.f, .05f);
        time += dt;

        if (tracksPending) {
            trackTimer += dt;
            if (trackTimer >= .10f) {
                regenerate();
                tracksPending = false;
                trackTimer = 0.f;
            }
        }

        if (firstPerson && !cinematicActive && !aiming && !targetLocked) {
            float yawError = std::remainder(
                sightYaw - turretYaw, glm::two_pi<float>()
            );

            float pitchError = sightPitch - turretPitch;

            turretYaw += glm::clamp(yawError, -dt * .85f, dt * .85f);
            turretPitch += glm::clamp(pitchError, -dt * .50f, dt * .50f);

            turretPitch = glm::clamp(
                turretPitch,
                glm::radians(-10.f),
                glm::radians(20.f)
            );
        }


        // Rotacao lenta da torre e elevacao limitada do canhao.
        if (aiming) {
            glm::vec2 delta = glm::vec2(queuedTarget.x, queuedTarget.z) - tankPosition;
            float distancia = glm::length(delta);
            float desiredYaw = distancia > .001f ? std::atan2(-delta.y, delta.x) : turretYaw;
            float baseY = height(tankPosition) +
                (hasAbrams ? (0.06f + (turretPivot.y - abramsBottom) * 0.56f) : 1.4f);
            float velocidade = velocidadeDaArma(queuedWeapon);
            float barrelLength = hasAbrams ? 3.32f : COMPRIMENTO_CANO;

            float vooAteAlvo = std::max(0.f, distancia - barrelLength) / velocidade;
            float correcaoQueda = .5f * GRAVIDADE * vooAteAlvo * vooAteAlvo;

            float desiredPitch = glm::clamp(
                std::atan2(queuedTarget.y - baseY + correcaoQueda, std::max(.05f, distancia)),
                glm::radians(-10.f),
                glm::radians(20.f)
            );

            float yawError = std::remainder(desiredYaw - turretYaw, glm::two_pi<float>());
            float pitchError = desiredPitch - turretPitch;

            turretYaw += glm::clamp(yawError, -dt * .85f, dt * .85f);
            turretPitch += glm::clamp(pitchError, -dt * .50f, dt * .50f);
            turretPitch = glm::clamp(turretPitch, glm::radians(-10.f), glm::radians(20.f));

            if (std::abs(yawError) < .025f && std::abs(pitchError) < .025f) {
                if (firstPerson && targetLocked) {
                    aiming = false;
                } else {
                glm::vec3 pivot{ tankPosition.x, baseY, tankPosition.y };

                glm::vec3 direcao{
                    std::cos(turretYaw) * std::cos(turretPitch),
                    std::sin(turretPitch),
                    -std::sin(turretYaw) * std::cos(turretPitch)
                };

                // A municao sempre nasce na ponta do canhao.
                glm::vec3 boca = pivot + direcao * barrelLength;
                glm::mat4 root = tankMatrix();
                glm::vec3 velocidadeInicial = direcao * velocidade;

                uint32_t id = nextShellId++;
                shells.push_back({ boca, velocidadeInicial, 0.f, queuedWeapon, id });
                shotTrails.push_back({ boca, velocidadeInicial, 0.f, 0.f, id, false });

                if (queuedCinematic) {
                    cinematicActive = true;
                    cinematicShellId = id;
                    cinematicHold = 0.f;

                    glm::vec3 lado = glm::normalize(glm::cross(direcao, glm::vec3(0, 1, 0)));
                    cinematicEye = boca - direcao * 3.5f + lado + glm::vec3(0, 1.f, 0);
                    cinematicLook = boca + direcao * 1.4f;
                }

                queuedCinematic = false;
                aiming = false;
                }
            }
        }

        for (auto& b : bursts) b.age += dt;
        bursts.erase(std::remove_if(bursts.begin(), bursts.end(),
            [](const Burst& b) { return b.age > 4.f; }), bursts.end());

        for (auto& tr : shotTrails)
            if (tr.finished) tr.fade += dt;

        // Camera cinematografica desacelera apenas o tempo de voo.
        float flightDt = dt * (cinematicActive ? .02f : 1.f);

        for (size_t i = 0; i < shells.size();) {
            auto& shell = shells[i];

            float anterior = shell.age;
            float proximo = anterior + flightDt;

            glm::vec3 p0 = pontoBalistico(shell.start, shell.velocity, anterior);
            glm::vec3 p1 = pontoBalistico(shell.start, shell.velocity, proximo);

            // Subdividir o trajeto evita atravessar o terreno em alta velocidade.
            float distanciaTrecho = glm::length(p1 - p0);
            int passos = std::max(1, int(std::ceil(distanciaTrecho / .15f)));

            bool atingiu = false;
            bool saiuMapa = false;
            glm::vec3 impacto{};
            float instanteFinal = proximo;

            for (int passo = 1; passo <= passos; ++passo) {
                float t = anterior + flightDt * (float(passo) / passos);
                glm::vec3 p = pontoBalistico(shell.start, shell.velocity, t);

                if (std::abs(p.x) >= terrainSize * .5f ||
                    std::abs(p.z) >= terrainSize * .5f) {
                    saiuMapa = true;
                    instanteFinal = t;
                    break;
                }

                if (p.y <= height({ p.x, p.z })) {
                    float lo = anterior + flightDt * (float(passo - 1) / passos);
                    float hi = t;

                    for (int k = 0; k < 8; ++k) {
                        float meio = (lo + hi) * .5f;
                        glm::vec3 q = pontoBalistico(shell.start, shell.velocity, meio);

                        if (q.y > height({ q.x, q.z })) lo = meio;
                        else hi = meio;
                    }

                    instanteFinal = hi;
                    impacto = pontoBalistico(shell.start, shell.velocity, instanteFinal);
                    impacto.y = height({ impacto.x, impacto.z });
                    atingiu = true;
                    break;
                }
            }

            shell.age = instanteFinal;

            auto trilha = std::find_if(shotTrails.begin(), shotTrails.end(),
                [&](const ShotTrail& tr) { return tr.shellId == shell.id; });

            if (trilha != shotTrails.end()) {
                trilha->flightTime = instanteFinal;
                if (atingiu || saiuMapa) trilha->finished = true;
            }

            if (atingiu) {
                int tipoAnterior = weapon;
                weapon = shell.kind;
                impact(impacto);
                weapon = tipoAnterior;
            }

            if (atingiu || saiuMapa) shells.erase(shells.begin() + i);
            else ++i;
        }

        shotTrails.erase(std::remove_if(shotTrails.begin(), shotTrails.end(),
            [](const ShotTrail& tr) { return tr.finished && tr.fade > .8f; }),
            shotTrails.end());

        // Camera persegue o projetil usando sua posicao fisica atual.
        if (cinematicActive) {
            auto found = std::find_if(shells.begin(), shells.end(),
                [&](const Shell& s) { return s.id == cinematicShellId; });

            if (found != shells.end()) {
                glm::vec3 pos = pontoBalistico(found->start, found->velocity, found->age);
                glm::vec3 v = found->velocity + glm::vec3(0.f, -GRAVIDADE * found->age, 0.f);
                glm::vec3 direcao = glm::normalize(v);
                glm::vec3 lado = glm::normalize(glm::cross(direcao, glm::vec3(0, 1, 0)));

                glm::vec3 eye = pos - direcao * 3.5f + lado + glm::vec3(0, 1.f, 0);
                eye.y = std::max(eye.y, height({ eye.x, eye.z }) + .5f);

                float suavidade = 1.f - std::exp(-dt * 10.f);
                cinematicEye = glm::mix(cinematicEye, eye, suavidade);
                cinematicLook = glm::mix(cinematicLook, pos + direcao * 1.4f, suavidade);
            }
            else {
                cinematicHold += dt;

                if (cinematicHold > .8f) {
                    cinematicActive = false;
                    cinematicShellId = 0;
                    recenterCamera();
                    followTank(0.f);
                }
            }
        }
    }


    void Simulation::reset() {
        craters.clear();
        shells.clear();
        shotTrails.clear();
        bursts.clear();

        tankPosition = { -8.f,-5.f };
        tankYaw = 0.f;
        turretYaw = 0.f;
        turretPitch = 0.f;
        trackLeftMotion = 0.f;
        trackRightMotion = 0.f;
        targetLocked = false;
        sightYaw = turretYaw;
        sightPitch = turretPitch;

        aiming = false;
        cinematicArmed = false;
        cinematicActive = false;
        queuedCinematic = false;
        cinematicShellId = 0;
        cinematicHold = 0.f;

        recenterCamera();
        followTank(0.f);

        std::fill(trackMarks.begin(), trackMarks.end(), 0.f);
        std::fill(trackRidges.begin(), trackRidges.end(), 0.f);
        tracksPending = false;
        trackTimer = 0.f;
        regenerate();
    }

    void Simulation::newTerrain() {
        terrainSeed = terrainSeed * 1664525u + 1013904223u;
        terrainBase.clear();
        reset();
    }

    std::vector<DrawObject> Simulation::objects() const {
        std::vector<DrawObject> out;
        out.push_back({ geometry.terrain,glm::mat4(1),{ 1,1,1,1 },1,0,false });

        auto add = [&](MeshRange mesh, glm::vec3 p, glm::vec3 size,
            glm::vec4 color, float material = 1, bool transparent = false) {
                out.push_back({ mesh,glm::translate(glm::mat4(1),p) *
                    glm::scale(glm::mat4(1),size),color,0,material,transparent });
            };

        float h = height(tankPosition);
        if (!firstPerson || cinematicActive || hasAbrams) {
            if (hasAbrams) {
                const float scale = 0.56f;
                glm::mat4 root = tankMatrix();

                auto rotateAround = [](glm::vec3 pivot, float radians, glm::vec3 axis) {
                    return glm::translate(glm::mat4(1.f), pivot) *
                        glm::rotate(glm::mat4(1.f), radians, axis) *
                        glm::translate(glm::mat4(1.f), -pivot);
                };

                glm::mat4 towerRotation = rotateAround(
                    turretPivot, turretYaw - tankYaw, glm::vec3(0, 1, 0));
                glm::mat4 gunRotation = rotateAround(
                    gunPivot, turretPitch, glm::vec3(0, 0, 1));

                for (const auto& part : geometry.parts) {
                    if (firstPerson && !cinematicActive) {
                        // Mostra apenas casco e canhao.
                        if (part.type != 0 && part.type != 2)
                            continue;
                    }
                    glm::mat4 transform = root;
                    if (part.type == 1) transform *= towerRotation;
                    if (part.type == 2) transform *= towerRotation * gunRotation;
                    if (part.type == 3 || part.type == 4) {
                        float movement = part.type == 3 ? trackLeftMotion : trackRightMotion;
                        transform *= rotateAround(part.pivot, movement / .35f,
                                                  glm::vec3(0, 0, 1));
                    }


                    if (firstPerson && !cinematicActive && part.type == 2) {
                        glm::mat4 cameraRotation =
                            glm::rotate(glm::mat4(1.f), sightYaw,
                                glm::vec3(0, 1, 0)) *
                            glm::rotate(glm::mat4(1.f), sightPitch,
                                glm::vec3(0, 0, 1));

                        transform =
                            glm::translate(glm::mat4(1.f), cameraPosition()) *
                            cameraRotation *
                            glm::translate(glm::mat4(1.f),
                                glm::vec3(0.15f, -0.25f, 0.65f)) *
                            glm::scale(glm::mat4(1.f), glm::vec3(0.56f)) *
                            glm::translate(glm::mat4(1.f), -gunPivot);
                    }


                    DrawObject object;
                    object.mesh = part.mesh;
                    object.model = transform;
                    object.tint = {1.f, 1.f, 1.f, 1.f};
                    object.material = (part.type == 5 || part.type == 6) ? 4.f : 3.f;
                    if (part.type == 5) object.uvShift = -trackLeftMotion * .10f;
                    if (part.type == 6) object.uvShift = -trackRightMotion * .10f;
                    out.push_back(object);
                }
            } else {

        // Casco e esteiras.
        glm::mat4 tank = glm::translate(
            glm::mat4(1), glm::vec3(tankPosition.x, h, tankPosition.y)
        ) * glm::rotate(glm::mat4(1), tankYaw, glm::vec3(0, 1, 0));

        auto addTank = [&](glm::vec3 p, glm::vec3 size, glm::vec4 color) {
            out.push_back({ geometry.cube,
                tank * glm::translate(glm::mat4(1),p) *
                glm::scale(glm::mat4(1),size),color,0,1,false });
            };

        addTank({ 0,.5f,0 }, { 2.8f,.75f,1.8f }, { .27f,.32f,.19f,1 });
        for (float z : {-1.f, 1.f})
            addTank({ 0,.22f,z }, { 2.9f,.5f,.35f }, { .09f,.10f,.08f,1 });

        // Torre independente do casco.
        glm::mat4 turret = glm::translate(
            glm::mat4(1), glm::vec3(tankPosition.x, h + 1.4f, tankPosition.y)
        ) * glm::rotate(glm::mat4(1), turretYaw, glm::vec3(0, 1, 0));

        out.push_back({ geometry.cube,
            turret * glm::translate(glm::mat4(1),glm::vec3(0,-.25f,0)) *
            glm::scale(glm::mat4(1),glm::vec3(1.3f,.6f,1.3f)),
            { .34f,.39f,.24f,1 },0,1,false });

        // Cano com elevacao da mira.
        glm::mat4 barrel = turret * glm::rotate(
            glm::mat4(1), turretPitch, glm::vec3(0, 0, 1)
        );

        out.push_back({ geometry.cube,
            barrel * glm::translate(glm::mat4(1),glm::vec3(1.175f,0,0)) *
            glm::scale(glm::mat4(1),glm::vec3(2.35f,.24f,.24f)),
            { .21f,.24f,.16f,1 },0,1,false });
            } // fallback: tanque antigo feito de cubos
        }

        // Linha vermelha acompanhando o caminho do disparo.

        for (const auto& tr : shotTrails) {
            float fade = 1.f - glm::clamp(tr.fade / .8f, 0.f, 1.f);
            float comprimento = glm::length(tr.velocity) * tr.flightTime;
            int segments = std::max(1, int(std::ceil(comprimento / .5f)));

            for (int j = 0; j < segments; ++j) {
                glm::vec3 p = pontoBalistico(
                    tr.start, tr.velocity, tr.flightTime * float(j) / segments
                );

                glm::vec3 q = pontoBalistico(
                    tr.start, tr.velocity, tr.flightTime * float(j + 1) / segments
                );

                glm::vec3 d = q - p;
                float tamanho = glm::length(d);
                if (tamanho < .001f) continue;

                glm::mat4 model =
                    glm::translate(glm::mat4(1), (p + q) * .5f) *
                    glm::mat4_cast(glm::rotation(glm::vec3(0, 1, 0), d / tamanho)) *
                    glm::scale(glm::mat4(1), glm::vec3(.04f, tamanho, .04f));

                out.push_back({
                    geometry.cube, model,{ fade, .02f, .015f, 1 }, 0, 2, false
                    });
            }
        }

        for (const auto& s : shells) {
            glm::vec3 p = pontoBalistico(s.start, s.velocity, s.age);
            add(geometry.sphere, p, { .15f, .15f, .15f }, { 1.f, .6f, .12f, 1.f }, 2);
        }


        // Explosoes originais.
        for (const auto& b : bursts) {
            float t = b.age;

            if (t < .35f) {
                float radius = .4f + t * 4;
                add(geometry.sphere, b.center + glm::vec3(0, .5f, 0),
                    { radius,radius,radius }, { 1,.42f,.07f,1 - t / .35f }, 2, true);
            }

            for (int k = 0; k < 9; k++) {
                float a = k * 2.399f;
                glm::vec3 p = b.center +
                    glm::vec3(std::cos(a), .7f + float(k % 3) * .3f, std::sin(a)) * t;
                float radius = .3f + t * .45f;
                add(geometry.sphere, p, { radius,radius,radius },
                    { .20f,.18f,.14f,.25f * (1 - t / 4) }, 1, true);
            }

            if (t < 1.2f) {
                for (int k = 0; k < 8; k++) {
                    float a = k * 2.399f;
                    glm::vec3 p = b.center + glm::vec3(
                        std::cos(a) * t * 3, (4 + k % 3) * t - 4.9f * t * t, std::sin(a) * t * 3
                    );

                    if (p.y > height({ p.x,p.z }))
                        add(geometry.cube, p, { .11f,.09f,.13f }, { .31f,.24f,.17f,1 });
                }
            }
        }


        if (firstPerson && !cinematicActive) {
            glm::vec3 forward = sightDirection();
            glm::vec3 right = glm::normalize(
                glm::cross(forward, glm::vec3(0, 1, 0))
            );
            glm::vec3 up = glm::normalize(glm::cross(right, forward));

            glm::mat4 base(1.f);
            base[0] = glm::vec4(right, 0.f);
            base[1] = glm::vec4(up, 0.f);
            base[2] = glm::vec4(-forward, 0.f);
            base[3] = glm::vec4(cameraPosition() + forward * 1.25f, 1.f);

            glm::vec4 color = targetLocked
                ? glm::vec4(1.f, .45f, .1f, 1.f)
                : glm::vec4(.15f, 1.f, .25f, 1.f);

            auto reticle = [&](float x, float y, float w, float h) {
                glm::mat4 model =
                    glm::translate(base, glm::vec3(x, y, 0.f)) *
                    glm::scale(glm::mat4(1.f), glm::vec3(w, h, .004f));

                out.push_back({
                    geometry.cube, model, color, 0.f, 2.f, false
                    });
                };

            reticle(-.045f, 0.f, .025f, .004f);
            reticle(.045f, 0.f, .025f, .004f);
            reticle(0.f, -.045f, .004f, .025f);
            reticle(0.f, .045f, .004f, .025f);
            reticle(0.f, 0.f, .005f, .005f);
        }


        glm::vec3 eye = cameraPosition();

        std::stable_sort(out.begin(), out.end(),
            [&](const DrawObject& a, const DrawObject& b) {
                if (a.transparent != b.transparent) return !a.transparent;
                if (!a.transparent) return false;
                return glm::length(glm::vec3(a.model[3]) - eye) >
                    glm::length(glm::vec3(b.model[3]) - eye);
            });

        return out;
    }

}
