#include <cstdint>
#include <iostream>
#include <string>

#include <SDL.h>

#include "rv/app/sdl_frontend.hpp"
#include "rv/devices/framebuffer.hpp"
#include "rv/devices/uart16550.hpp"

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

void push_printable(
    SDL_Keycode key,
    const char* text,
    SDL_Keymod modifiers = KMOD_NONE)
{
    push_key(key, modifiers);
    push_text(text);
}

void push_mouse_button(
    std::uint32_t type,
    std::uint8_t button,
    int x,
    int y)
{
    SDL_Event event{};
    event.type = type;
    event.button.type = type;
    event.button.button = button;
    event.button.x = x;
    event.button.y = y;
    CHECK(SDL_PushEvent(&event) == 1);
}

void push_mouse_motion(int x, int y)
{
    SDL_Event event{};
    event.type = SDL_MOUSEMOTION;
    event.motion.type = SDL_MOUSEMOTION;
    event.motion.state = SDL_BUTTON_LMASK;
    event.motion.x = x;
    event.motion.y = y;
    CHECK(SDL_PushEvent(&event) == 1);
}

void test_window_input_and_framebuffer()
{
    CHECK(SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
    CHECK(SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software") == SDL_TRUE);

    rv::app::SdlFrontend frontend;
    CHECK(frontend.ready());
    CHECK(frontend.active());
    CHECK(frontend.error().empty());
    CHECK(frontend.view() == rv::app::DisplayView::Terminal);

    push_printable(SDLK_a, "a");
    push_printable(SDLK_b, "b");
    push_printable(SDLK_c, "c");
    push_printable(SDLK_1, "!", KMOD_SHIFT);
    push_printable(SDLK_KP_0, "0");
    push_printable(SDLK_KP_9, "9");
    push_key(SDLK_RETURN);
    push_key(SDLK_UP);
    push_key(SDLK_c, KMOD_CTRL);

    const std::string input = frontend.poll_input();
    CHECK(input == "abc!09\r\x1B[A\x03");

    std::string expected_burst;
    for (int index = 0; index < 200; ++index) {
        const char character =
            static_cast<char>('a' + index % 26);
        const char text[]{character, '\0'};
        push_printable(
            static_cast<SDL_Keycode>(SDLK_a + index % 26),
            text);
        expected_burst.push_back(character);
    }
    CHECK(frontend.poll_input() == expected_burst);

    rv::devices::Uart16550 uart(0x10000000ULL, 0x100U);
    uart.inject_received(input);
    for (const char expected : input) {
        const auto received =
            uart.read(0, rv::AccessWidth::Byte);
        CHECK(received.ok());
        CHECK(
            received.value ==
            static_cast<unsigned char>(expected));
    }

    rv::devices::Framebuffer framebuffer(
        0x40000000ULL,
        640U,
        480U,
        4U);
    CHECK(
        framebuffer.write(
            0,
            rv::AccessWidth::Word,
            0x00112233U) ==
        rv::BusFault::None);
    CHECK(framebuffer.dirty());

    frontend.append_uart("UART terminal output\r\n");
    frontend.present(&framebuffer);
    CHECK(framebuffer.dirty());

    push_mouse_button(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, 1, 1);
    push_mouse_motion(3 * 8 + 1, 1);
    push_mouse_button(
        SDL_MOUSEBUTTONUP,
        SDL_BUTTON_LEFT,
        3 * 8 + 1,
        1);
    CHECK(frontend.poll_input().empty());
    push_mouse_button(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT, 1, 1);
    CHECK(frontend.poll_input().empty());
    char* copied = SDL_GetClipboardText();
    CHECK(copied != nullptr);
    if (copied != nullptr) {
        CHECK(std::string(copied) == "UART");
        SDL_free(copied);
    }

    CHECK(SDL_SetClipboardText("echo ok\r\npwd\n") == 0);
    push_key(SDLK_v, static_cast<SDL_Keymod>(KMOD_CTRL | KMOD_SHIFT));
    CHECK(frontend.poll_input() == "echo ok\rpwd\r");

    push_mouse_button(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, 1, 1);
    push_mouse_button(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, 1, 1);
    CHECK(frontend.poll_input().empty());
    CHECK(SDL_SetClipboardText("right click\n") == 0);
    push_mouse_button(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT, 1, 1);
    CHECK(frontend.poll_input() == "right click\r");

    push_key(SDLK_F2);
    CHECK(frontend.poll_input().empty());
    CHECK(frontend.view() == rv::app::DisplayView::Framebuffer);
    frontend.present(&framebuffer);
    CHECK(!framebuffer.dirty());
    CHECK(
        frontend.performance_counters().full_texture_uploads >=
        2U);

    constexpr std::uint64_t changed_pixel =
        (20ULL * 640ULL + 10ULL) * 4ULL;
    CHECK(
        framebuffer.write(
            changed_pixel,
            rv::AccessWidth::Word,
            0x00445566U) ==
        rv::BusFault::None);
    SDL_Delay(20U);
    const auto partial_before =
        frontend.performance_counters().partial_texture_uploads;
    frontend.present(&framebuffer);
    CHECK(!framebuffer.dirty());
    CHECK(
        frontend.performance_counters().partial_texture_uploads ==
        partial_before + 1U);
    CHECK(
        frontend.performance_counters().uploaded_bytes >=
        4U);

    push_key(SDLK_F1);
    static_cast<void>(frontend.poll_input());
    CHECK(frontend.view() == rv::app::DisplayView::Terminal);
    frontend.present(&framebuffer);

    for (int index = 0; index < 16; ++index) {
        push_key(SDLK_F2);
        push_key(SDLK_F1);
    }
    push_key(SDLK_F2);
    CHECK(frontend.poll_input().empty());
    CHECK(frontend.view() == rv::app::DisplayView::Framebuffer);
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
