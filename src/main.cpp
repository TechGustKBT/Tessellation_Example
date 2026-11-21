
#include "VulkanRenderer.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <sstream>

using namespace simulator;

struct App {
    Simulation simulation;
    VulkanRenderer* renderer = nullptr;
    double mouseX = 0, mouseY = 0;
    bool mouseKnown = false;
    float timeScale = 1.f;
    double lastRightClick = -1.0;
};

static App& app(GLFWwindow* window) {
    return *static_cast<App*>(glfwGetWindowUserPointer(window));
}

static void keys(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;

    auto& a = app(window);
    auto& s = a.simulation;

    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        break;
    case GLFW_KEY_SPACE:
        s.paused = !s.paused;
        break;
    case GLFW_KEY_1:
        s.weapon = 0;
        break;
    case GLFW_KEY_2:
        s.weapon = 1;
        break;
    case GLFW_KEY_F5:
        s.newTerrain();
        break;
    case GLFW_KEY_C:
    case GLFW_KEY_HOME:
        s.recenterCamera();
        break;
    case GLFW_KEY_V:
        if (s.cinematicActive) {
            s.cinematicActive = false;
            s.cinematicShellId = 0;
            s.recenterCamera();
        }
        else {
            s.cinematicArmed = !s.cinematicArmed;
        }
        break;
    case GLFW_KEY_F1:
        if (a.renderer->wireframeSupported())
            s.wireframe = !s.wireframe;
        break;
    case GLFW_KEY_F2:
        s.normals = !s.normals;
        break;
    case GLFW_KEY_F3:
        s.adaptive = !s.adaptive;
        break;
    case GLFW_KEY_EQUAL:
    case GLFW_KEY_KP_ADD:
        s.maxTess = std::min(s.maxTess * 2,
            float(std::min(64u, a.renderer->maxTessellation())));
        break;
    case GLFW_KEY_MINUS:
    case GLFW_KEY_KP_SUBTRACT:
        s.maxTess = std::max(s.maxTess * .5f, 2.f);
        break;
    case GLFW_KEY_T:
        a.timeScale = std::max(0.03125f, a.timeScale * 0.5f);
        break;

    case GLFW_KEY_Y:
        a.timeScale = 1.f;
        break;
    }
}


static void move(GLFWwindow* window, double x, double y) {
    auto& a = app(window);
    auto& s = a.simulation;

    if (a.mouseKnown && !s.cinematicActive) {
        float dx = float(x - a.mouseX);
        float dy = float(y - a.mouseY);

        if (s.firstPerson) {
            s.sightYaw -= dx * .0045f;

            s.sightPitch = glm::clamp(
                s.sightPitch - dy * .0045f,
                glm::radians(-10.f),
                glm::radians(20.f)
            );

            // Movimentar a mira cancela o alvo previamente fixado.
            if (s.targetLocked && std::abs(dx) + std::abs(dy) > 2.f) {
                s.targetLocked = false;
                s.aiming = false;
            }
        }
        else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
            s.cameraYaw -= dx * .0045f;

            s.cameraPitch = std::clamp(
                s.cameraPitch + dy * .0045f,
                -1.2f, 1.3f
            );
        }
    }

    a.mouseX = x;
    a.mouseY = y;
    a.mouseKnown = true;
}



static void click(GLFWwindow* window, int button, int action, int) {
    auto& a = app(window);
    auto& s = a.simulation;

    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            double now = glfwGetTime();

            // Dois cliques direitos alternam a primeira pessoa.
            if (a.lastRightClick >= 0.0 &&
                now - a.lastRightClick <= .32) {

                s.firstPerson = !s.firstPerson;
                s.targetLocked = false;
                s.aiming = false;

                if (s.firstPerson) {
                    s.sightYaw = s.turretYaw;
                    s.sightPitch = s.turretPitch;
                }
                else {
                    s.recenterCamera();
                    s.followTank(0.f);
                }

                glfwSetInputMode(
                    window,
                    GLFW_CURSOR,
                    s.firstPerson ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL
                );

                a.lastRightClick = -1.0;
                a.mouseKnown = false;
                return;
            }

            a.lastRightClick = now;

            if (!s.firstPerson)
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
        else if (action == GLFW_RELEASE && !s.firstPerson) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }

        a.mouseKnown = false;
        return;
    }

    if (button != GLFW_MOUSE_BUTTON_LEFT ||
        action != GLFW_PRESS ||
        s.paused ||
        s.cinematicActive)
        return;

    int w = 1, h = 1;
    glfwGetWindowSize(window, &w, &h);

    if (s.firstPerson) {
        if (!s.targetLocked) {
            // Primeiro clique: fixa o alvo no centro da reticula.
            s.queuedTarget = s.pick(w * .5, h * .5, w, h);
            s.queuedWeapon = s.weapon;
            s.targetLocked = true;
            s.aiming = true;
        }
        else {
            // Segundo clique: confirma o disparo.
            s.targetLocked = false;
            s.queuedCinematic = s.cinematicArmed;
            s.cinematicArmed = false;
            s.aiming = true;
        }

        return;
    }

    // Disparo original da terceira pessoa.
    double x = 0, y = 0;

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        x = w * .5;
        y = h * .5;
    }
    else {
        glfwGetCursorPos(window, &x, &y);
    }

    s.fire(s.pick(x, y, w, h));
}


static void input(GLFWwindow* window, Simulation& s, float dt) {
    if (!glfwGetWindowAttrib(window, GLFW_FOCUSED)) return;

    auto down = [&](int key) {
        return glfwGetKey(window, key) == GLFW_PRESS;
        };

    s.moveTank(
        float(down(GLFW_KEY_W)) - float(down(GLFW_KEY_S)),
        float(down(GLFW_KEY_A)) - float(down(GLFW_KEY_D)),
        dt,
        down(GLFW_KEY_LEFT_SHIFT) || down(GLFW_KEY_RIGHT_SHIFT)
    );

    if (!s.cinematicActive && !s.firstPerson) {
        s.cameraYaw +=
            (float(down(GLFW_KEY_LEFT)) - float(down(GLFW_KEY_RIGHT))) * dt * 1.6f;

        s.cameraPitch = std::clamp(
            s.cameraPitch +
            (float(down(GLFW_KEY_DOWN)) - float(down(GLFW_KEY_UP))) * dt * 1.2f,
            -1.2f, 1.3f
        );
    }
}

int main(int argc, char** argv) {
    Options options;
    auto executableDir = std::filesystem::absolute(argv[0]).parent_path();
    options.shaderDir = executableDir / "shaders";
    // Aceita assets/ ao lado do .exe OU na raiz do projeto do Visual Studio.
    for (auto dir = executableDir; !dir.empty(); dir = dir.parent_path()) {
        if (std::filesystem::exists(dir / "assets" / "Abrams_Model.tasm")) {
            options.assetDir = dir / "assets";
            break;
        }
        if (dir == dir.root_path() || dir == dir.parent_path()) break;
    }
    if (options.assetDir.empty()) options.assetDir = std::filesystem::current_path() / "assets";
    bool wire = false, normals = false;
    float tess = 16;

    try {
        auto value = [&](int& i) {
            if (++i >= argc) throw std::runtime_error("Falta valor do argumento.");
            return std::string(argv[i]);
            };

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--frames") {
                options.frames = std::stoi(value(i));
                if (options.frames < 1) throw std::runtime_error("--frames deve ser positivo.");
            }
            else if (arg == "--smoke-test") options.smoke = true;
            else if (arg == "--validation") options.validation = true;
            else if (arg == "--require-validation") {
                options.validation = true;
                options.requireValidation = true;
            }
            else if (arg == "--no-validation") options.validation = false;
            else if (arg == "--shaders") options.shaderDir = value(i);
            else if (arg == "--capture") options.capture = value(i);
            else if (arg == "--wireframe") wire = true;
            else if (arg == "--normals") normals = true;
            else if (arg == "--tess") tess = std::stof(value(i));
            else if (arg == "--help") {
                std::cout << "Tessellation Simulator Vulkan\n";
                return 0;
            }
            else throw std::runtime_error("Argumento desconhecido: " + arg);
        }

        if (options.requireValidation) options.validation = true;
        if (options.smoke && !options.frames) options.frames = 180;
        if (!options.capture.empty() && !options.frames) options.frames = 1;

        glfwSetErrorCallback([](int, const char* msg) {
            std::cerr << "GLFW: " << msg << '\n';
            });

        if (!glfwInit()) throw std::runtime_error("GLFW nao inicializou.");

        struct Terminate {
            ~Terminate() { glfwTerminate(); }
        } terminate;

        if (!glfwVulkanSupported())
            throw std::runtime_error("Driver Vulkan nao encontrado.");

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
            glfwCreateWindow(1280, 800, "Tessellation Simulator Vulkan",
                nullptr, nullptr), glfwDestroyWindow
        );

        if (!window) throw std::runtime_error("Falha ao abrir janela.");

        App a;
        a.simulation.loadAbrams((options.assetDir / "Abrams_Model.tasm").string());
        VulkanRenderer renderer(window.get(), options);
        a.renderer = &renderer;

        a.simulation.recenterCamera();
        a.simulation.followTank(0.f);

        renderer.initialize(a.simulation);
        a.simulation.dirty = false;
        a.simulation.maxTess = std::min(tess,
            float(std::min(64u, renderer.maxTessellation())));
        a.simulation.wireframe = wire && renderer.wireframeSupported();
        a.simulation.normals = normals;

        glfwSetWindowUserPointer(window.get(), &a);
        glfwSetKeyCallback(window.get(), keys);
        glfwSetCursorPosCallback(window.get(), move);
        glfwSetMouseButtonCallback(window.get(), click);

        glfwSetScrollCallback(window.get(),
            [](GLFWwindow* w, double, double y) {
                auto& s = app(w).simulation;
                s.cameraDistance = std::clamp(
                    s.cameraDistance * std::exp(float(-y) * .12f), 7.f, 75.f);
            });

        glfwSetFramebufferSizeCallback(window.get(),
            [](GLFWwindow* w, int, int) {
                app(w).renderer->resize();
            });

        std::cout << "WASD: tanque | Botao direito: camera 360\n";
        std::cout << "Clique esquerdo: mirar e disparar | V: camera cinematografica\n";

        if (options.smoke) {
            a.simulation.impact({ 1, 0, 0 });
            a.simulation.impact({ 5, 0, 4 });
            a.simulation.fire({ -2, 0, 3 });
        }

        auto previous = std::chrono::steady_clock::now();
        float titleTimer = 1;
        int rendered = 0, iteration = 0;

        while (!glfwWindowShouldClose(window.get()) &&
            (!options.frames || rendered < options.frames)) {

            glfwPollEvents();
            auto now = std::chrono::steady_clock::now();
            float dt = std::clamp(
                std::chrono::duration<float>(now - previous).count(),
                0.f, .05f);
            previous = now;

            if (options.smoke) dt = 1.f / 60.f;

            float gameDt = dt * a.timeScale;

            input(window.get(), a.simulation, gameDt);
            a.simulation.tick(gameDt);
            a.simulation.followTank(gameDt);

            if (options.smoke) {
                if (iteration == 40) glfwSetWindowSize(window.get(), 1024, 768);
                if (iteration == 80) a.simulation.wireframe = renderer.wireframeSupported();
                if (iteration == 100) a.simulation.wireframe = false;
                if (iteration == 120) a.simulation.normals = true;
                if (iteration == 140) a.simulation.normals = false;
                if (iteration == 160) a.simulation.impact({ 3, 0, -1 });
            }

            titleTimer += dt;
            if (titleTimer > .2f) {
                std::ostringstream title;
                title << "Tessellation Simulator | " 
                    << "Velocidade: " << (a.timeScale * 100.f) << "% | "
                    << (a.simulation.weapon ? "Artilharia" : "Tanque")
                    << " | Tess " << a.simulation.maxTess
                    << (a.simulation.adaptive ? " adaptativa" : " fixa")
                    << (a.simulation.aiming ? " | MIRANDO" : "")
                    << (a.simulation.cinematicActive ? " | CINEMATICA" :
                        a.simulation.cinematicArmed ? " | CINEMATICA ARMADA" : "")
                    << " | " << renderer.gpuName();

                glfwSetWindowTitle(window.get(), title.str().c_str());
                titleTimer = 0;
            }

            bool capture = !options.capture.empty() &&
                rendered == options.frames - 1;

            if (renderer.draw(a.simulation, capture)) ++rendered;
            ++iteration;
        }

        renderer.shutdown();
        std::cout << "Frames: " << rendered
            << " | Erros Vulkan: " << renderer.validationErrors() << '\n';
        return renderer.validationErrors() ? 2 : 0;

    }
    catch (const std::exception& error) {
        std::cerr << "ERRO: " << error.what() << '\n';
        return 1;
    }
}
