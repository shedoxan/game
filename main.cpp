#include "renderer.hpp"
#include "presenter.hpp"
#include "ai.hpp"
#include "threadpool.h"

int main() {
    ThreadPool pool(std::thread::hardware_concurrency());
    chess::SearchOptions opt; opt.maxDepth = 25; opt.timeMs = 1000;
    chess::AIEngine  engine(pool, opt);
    gui::Renderer    renderer(1024, 1024);          // Квадратное окно
    gui::Presenter   presenter(renderer, engine, pool);

    while (!renderer.shouldClose()) {
        renderer.pollEvents();

        presenter.update();
    }
    return 0;
}
