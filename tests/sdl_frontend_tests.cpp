#include <cstdint>
#include <iostream>
#include <string>

#include <SDL.h>

#include "rv32/app/sdl_frontend.hpp"
#include "rv32/devices/framebuffer.hpp"
#include "rv32/devices/uart16550.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": "     \
                      << #condition << '\n';                                 \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

void push_key(SDL_Keycode key, SDL_Keymod modifiers = KMOD_NONE)
{
    SDL_Event event{};
    event.type = SDL_KEYDOWN;
    event.key.type = SDL_KEYDOWN;
    event.key.state = SDL_PRESSED;
    event.key.keysym.sym = key;
    event.key.keysym.mod = modifiers;
    CHECK(SDL_PushEvent(&event) == 1);
}

void push_text(const char* value)
{
    SDL_Event event{};
    event.type = SDL_TEXTINPUT;
    event.text.type = SDL_TEXTINPUT;
    static_cast<void>(
        SDL_strlcpy(
            event.text.text,
            value,
            sizeof(event.text.text)));
    CHECK(SDL_PushEvent(&event) == 1);
}

void test_window_input_and_framebuffer()
{
    CHECK(SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
    CHECK(SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software") == SDL_TRUE);

    rv32::app::SdlFrontend frontend;
    CHECK(frontend.ready());
    CHECK(frontend.active());
    CHECK(frontend.error().empty());
    CHECK(frontend.view() == rv32::app::DisplayView::Terminal);

    push_key(SDLK_a);
    push_text("a");
    push_key(SDLK_b);
    push_key(SDLK_c);
    push_key(SDLK_1, KMOD_SHIFT);
    push_key(SDLK_KP_0);
    push_key(SDLK_KP_9);
    push_key(SDLK_RETURN);
    push_key(SDLK_UP);
    push_key(SDLK_c, KMOD_CTRL);

    const std::string input = frontend.poll_input();
    CHECK(input == "abc!09\r\x1B[A\x03");

    rv32::devices::Uart16550 uart(0x10000000ULL, 0x100U);
    uart.inject_received(input);
    for (const char expected : input) {
        const auto received =
            uart.read(0, rv32::AccessWidth::Byte);
        CHECK(received.ok());
        CHECK(
            received.value ==
            static_cast<unsigned char>(expected));
    }

    rv32::devices::Framebuffer framebuffer(
        0x40000000ULL,
        16U,
        8U,
        4U);
    CHECK(
        framebuffer.write(
            0,
            rv32::AccessWidth::Word,
            0x00112233U) ==
        rv32::BusFault::None);
    CHECK(framebuffer.dirty());

    frontend.append_uart("UART terminal output\r\n");
    frontend.present(&framebuffer);
    CHECK(framebuffer.dirty());

    push_key(SDLK_F2);
    CHECK(frontend.poll_input().empty());
    CHECK(frontend.view() == rv32::app::DisplayView::Framebuffer);
    frontend.present(&framebuffer);
    CHECK(!framebuffer.dirty());

    push_key(SDLK_F1);
    static_cast<void>(frontend.poll_input());
    CHECK(frontend.view() == rv32::app::DisplayView::Terminal);
    frontend.present(&framebuffer);

    for (int index = 0; index < 16; ++index) {
        push_key(SDLK_F2);
        push_key(SDLK_F1);
    }
    push_key(SDLK_F2);
    CHECK(frontend.poll_input().empty());
    CHECK(frontend.view() == rv32::app::DisplayView::Framebuffer);
    frontend.present(&framebuffer);

    SDL_Event quit{};
    quit.type = SDL_QUIT;
    CHECK(SDL_PushEvent(&quit) == 1);
    static_cast<void>(frontend.poll_input());
    CHECK(!frontend.active());
}

} // namespace

int main()
{
    test_window_input_and_framebuffer();

    if (failures == 0) {
        std::cout << "All SDL frontend tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
