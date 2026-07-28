#include "rv32/app/sdl_frontend.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <SDL.h>

#include "font8x8_basic.hpp"
#include "rv32/app/terminal_console.hpp"
#include "rv32/devices/framebuffer.hpp"

namespace rv32::app {

namespace {

constexpr int logical_width = 640;
constexpr int logical_height = 480;
constexpr std::uint32_t terminal_background = 0xFF101820U;
constexpr std::uint32_t terminal_foreground = 0xFFD8E5E8U;
constexpr std::uint32_t terminal_cursor = 0xFF66D9EFU;
constexpr std::uint64_t text_input_suppression_timeout_ms = 250U;
constexpr std::size_t maximum_suppressed_text_size = 64U;
constexpr std::uint64_t display_refresh_interval_ms = 16U;

[[nodiscard]] std::string ascii_key_text(
    SDL_Keycode key,
    SDL_Keymod modifiers)
{
    if ((modifiers & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) != 0) {
        return {};
    }

    const bool shift = (modifiers & KMOD_SHIFT) != 0;
    const bool caps_lock = (modifiers & KMOD_CAPS) != 0;
    if (key >= SDLK_a && key <= SDLK_z) {
        char character = static_cast<char>('a' + (key - SDLK_a));
        if (shift != caps_lock) {
            character = static_cast<char>('A' + (key - SDLK_a));
        }
        return std::string(1U, character);
    }

    if (key >= SDLK_0 && key <= SDLK_9) {
        constexpr std::string_view shifted_digits = ")!@#$%^&*(";
        const auto index = static_cast<std::size_t>(key - SDLK_0);
        return std::string(
            1U,
            shift
                ? shifted_digits[index]
                : static_cast<char>('0' + index));
    }

    if (key == SDLK_KP_0) {
        return "0";
    }
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) {
        return std::string(
            1U,
            static_cast<char>('1' + (key - SDLK_KP_1)));
    }

    char character{};
    switch (key) {
    case SDLK_SPACE:
        character = ' ';
        break;
    case SDLK_MINUS:
        character = shift ? '_' : '-';
        break;
    case SDLK_EQUALS:
        character = shift ? '+' : '=';
        break;
    case SDLK_LEFTBRACKET:
        character = shift ? '{' : '[';
        break;
    case SDLK_RIGHTBRACKET:
        character = shift ? '}' : ']';
        break;
    case SDLK_BACKSLASH:
        character = shift ? '|' : '\\';
        break;
    case SDLK_SEMICOLON:
        character = shift ? ':' : ';';
        break;
    case SDLK_QUOTE:
        character = shift ? '"' : '\'';
        break;
    case SDLK_BACKQUOTE:
        character = shift ? '~' : '`';
        break;
    case SDLK_COMMA:
        character = shift ? '<' : ',';
        break;
    case SDLK_PERIOD:
        character = shift ? '>' : '.';
        break;
    case SDLK_SLASH:
        character = shift ? '?' : '/';
        break;
    case SDLK_KP_PERIOD:
        character = '.';
        break;
    case SDLK_KP_DIVIDE:
        character = '/';
        break;
    case SDLK_KP_MULTIPLY:
        character = '*';
        break;
    case SDLK_KP_MINUS:
        character = '-';
        break;
    case SDLK_KP_PLUS:
        character = '+';
        break;
    case SDLK_KP_EQUALS:
        character = '=';
        break;
    default:
        break;
    }
    return character == '\0'
               ? std::string{}
               : std::string(1U, character);
}

[[nodiscard]] std::uint32_t read_pixel(
    const devices::Framebuffer& framebuffer,
    std::uint32_t x,
    std::uint32_t y) noexcept
{
    const auto source = framebuffer.pixels();
    const std::uint64_t pixel_index =
        static_cast<std::uint64_t>(y) * framebuffer.width() + x;
    const std::uint64_t offset =
        pixel_index * framebuffer.bytes_per_pixel();
    const auto bytes_per_pixel = framebuffer.bytes_per_pixel();
    if (offset > source.size() ||
        bytes_per_pixel > source.size() - offset) {
        return 0xFF000000U;
    }

    const auto index = static_cast<std::size_t>(offset);
    if (bytes_per_pixel >= 3U) {
        const std::uint32_t blue = source[index];
        const std::uint32_t green = source[index + 1U];
        const std::uint32_t red = source[index + 2U];
        return 0xFF000000U | (red << 16U) |
               (green << 8U) | blue;
    }
    if (bytes_per_pixel == 2U) {
        const std::uint16_t rgb565 =
            static_cast<std::uint16_t>(source[index]) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(source[index + 1U]) << 8U);
        const std::uint32_t red =
            static_cast<std::uint32_t>((rgb565 >> 11U) & 0x1FU) * 255U /
            31U;
        const std::uint32_t green =
            static_cast<std::uint32_t>((rgb565 >> 5U) & 0x3FU) * 255U /
            63U;
        const std::uint32_t blue =
            static_cast<std::uint32_t>(rgb565 & 0x1FU) * 255U /
            31U;
        return 0xFF000000U | (red << 16U) |
               (green << 8U) | blue;
    }

    const std::uint32_t level = source[index];
    return 0xFF000000U | (level << 16U) |
           (level << 8U) | level;
}

} // namespace

class SdlFrontend::Impl {
  public:
    Impl()
        : pixels_(
              static_cast<std::size_t>(logical_width) *
                  static_cast<std::size_t>(logical_height),
              terminal_background)
    {
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
            set_error("SDL initialization failed");
            return;
        }
        owns_sdl_ = true;

        static_cast<void>(
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest"));
        window_ = SDL_CreateWindow(
            "RISC-V32 CPU Emulator - UART Terminal (F1) / Framebuffer (F2)",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            logical_width,
            logical_height,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (window_ == nullptr) {
            set_error("SDL window creation failed");
            return;
        }

        renderer_ = SDL_CreateRenderer(
            window_,
            -1,
            SDL_RENDERER_ACCELERATED);
        if (renderer_ == nullptr) {
            renderer_ = SDL_CreateRenderer(
                window_,
                -1,
                SDL_RENDERER_SOFTWARE);
        }
        if (renderer_ == nullptr) {
            set_error("SDL renderer creation failed");
            return;
        }
        if (SDL_RenderSetLogicalSize(
                renderer_,
                logical_width,
                logical_height) != 0) {
            set_error("SDL logical-size setup failed");
            return;
        }

        texture_ = SDL_CreateTexture(
            renderer_,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            logical_width,
            logical_height);
        if (texture_ == nullptr) {
            set_error("SDL texture creation failed");
            return;
        }
        if (SDL_SetTextureBlendMode(
                texture_,
                SDL_BLENDMODE_NONE) != 0) {
            set_error("SDL texture blend-mode setup failed");
            return;
        }

        SDL_StartTextInput();
        SDL_RaiseWindow(window_);
        static_cast<void>(SDL_SetWindowInputFocus(window_));
        ready_ = true;
        active_ = true;
    }

    ~Impl()
    {
        if (owns_sdl_) {
            SDL_StopTextInput();
        }
        if (texture_ != nullptr) {
            SDL_DestroyTexture(texture_);
        }
        if (renderer_ != nullptr) {
            SDL_DestroyRenderer(renderer_);
        }
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
        }
        if (owns_sdl_) {
            SDL_Quit();
        }
    }

    [[nodiscard]] bool ready() const noexcept
    {
        return ready_;
    }

    [[nodiscard]] bool active() const noexcept
    {
        return active_;
    }

    [[nodiscard]] std::string_view error() const noexcept
    {
        return error_;
    }

    [[nodiscard]] DisplayView view() const noexcept
    {
        return view_;
    }

    void append_uart(std::string_view output)
    {
        terminal_.append(output);
    }

    [[nodiscard]] std::string poll_input()
    {
        std::string input;
        if (!ready_ || !active_) {
            return input;
        }

        SDL_Event event{};
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT &&
                 event.window.event == SDL_WINDOWEVENT_CLOSE &&
                 event.window.windowID == SDL_GetWindowID(window_))) {
                active_ = false;
                SDL_HideWindow(window_);
                continue;
            }
            if (event.type == SDL_WINDOWEVENT &&
                (event.window.event == SDL_WINDOWEVENT_EXPOSED ||
                 event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                force_redraw_ = true;
                continue;
            }
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                SDL_StartTextInput();
                continue;
            }
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                SDL_StopTextInput();
                continue;
            }
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                static_cast<void>(SDL_SetWindowInputFocus(window_));
                continue;
            }
            if (event.type == SDL_TEXTINPUT) {
                append_text_input(input, event.text.text);
                continue;
            }
            if (event.type != SDL_KEYDOWN) {
                continue;
            }

            const SDL_Keycode key = event.key.keysym.sym;
            if (key == SDLK_F1) {
                set_view(DisplayView::Terminal);
                continue;
            }
            if (key == SDLK_F2) {
                set_view(DisplayView::Framebuffer);
                continue;
            }

            const bool control =
                (event.key.keysym.mod & KMOD_CTRL) != 0;
            if (control && key >= SDLK_a && key <= SDLK_z) {
                input.push_back(static_cast<char>(
                    key - SDLK_a + 1));
                continue;
            }

            const auto key_text = ascii_key_text(
                key,
                static_cast<SDL_Keymod>(event.key.keysym.mod));
            if (!key_text.empty()) {
                input.append(key_text);
                remember_key_text(key_text);
                continue;
            }

            switch (key) {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                input.push_back('\r');
                break;
            case SDLK_BACKSPACE:
                input.push_back('\x7F');
                break;
            case SDLK_TAB:
                input.push_back('\t');
                break;
            case SDLK_ESCAPE:
                input.push_back('\x1B');
                break;
            case SDLK_UP:
                input.append("\x1B[A");
                break;
            case SDLK_DOWN:
                input.append("\x1B[B");
                break;
            case SDLK_RIGHT:
                input.append("\x1B[C");
                break;
            case SDLK_LEFT:
                input.append("\x1B[D");
                break;
            case SDLK_HOME:
                input.append("\x1B[H");
                break;
            case SDLK_END:
                input.append("\x1B[F");
                break;
            case SDLK_DELETE:
                input.append("\x1B[3~");
                break;
            case SDLK_PAGEUP:
                input.append("\x1B[5~");
                break;
            case SDLK_PAGEDOWN:
                input.append("\x1B[6~");
                break;
            default:
                break;
            }
        }
        return input;
    }

    void present(devices::Framebuffer* framebuffer)
    {
        if (!ready_ || !active_) {
            return;
        }

        const bool content_dirty =
            view_ == DisplayView::Terminal
                ? terminal_.dirty()
                : framebuffer != nullptr && framebuffer->dirty();
        if (!force_redraw_ && !content_dirty) {
            return;
        }

        const std::uint64_t now = SDL_GetTicks64();
        if (!force_redraw_ &&
            has_presented_ &&
            now - last_present_ticks_ <
                display_refresh_interval_ms) {
            return;
        }

        const void* texture_pixels = pixels_.data();
        int texture_pitch =
            logical_width *
            static_cast<int>(sizeof(std::uint32_t));
        if (view_ == DisplayView::Terminal) {
            render_terminal();
        } else if (can_upload_directly(framebuffer)) {
            texture_pixels = framebuffer->pixels().data();
            texture_pitch =
                static_cast<int>(
                    framebuffer->width() *
                    framebuffer->bytes_per_pixel());
        } else {
            render_framebuffer(framebuffer);
        }

        if (SDL_UpdateTexture(
                texture_,
                nullptr,
                texture_pixels,
                texture_pitch) != 0) {
            set_error("SDL texture update failed");
            return;
        }
        if (SDL_RenderClear(renderer_) != 0 ||
            SDL_RenderCopy(renderer_, texture_, nullptr, nullptr) != 0) {
            set_error("SDL rendering failed");
            return;
        }
        SDL_RenderPresent(renderer_);

        if (view_ == DisplayView::Terminal) {
            terminal_.clear_dirty();
        } else if (framebuffer != nullptr) {
            framebuffer->clear_dirty();
        }
        force_redraw_ = false;
        has_presented_ = true;
        last_present_ticks_ = SDL_GetTicks64();
    }

  private:
    void expire_suppressed_text()
    {
        if (suppressed_text_input_.empty()) {
            return;
        }

        const std::uint64_t now = SDL_GetTicks64();
        if (now - last_key_text_ticks_ >
            text_input_suppression_timeout_ms) {
            suppressed_text_input_.clear();
        }
    }

    void remember_key_text(std::string_view text)
    {
        expire_suppressed_text();
        suppressed_text_input_.append(text);
        if (suppressed_text_input_.size() >
            maximum_suppressed_text_size) {
            suppressed_text_input_.erase(
                0,
                suppressed_text_input_.size() -
                    maximum_suppressed_text_size);
        }
        last_key_text_ticks_ = SDL_GetTicks64();
    }

    void append_text_input(
        std::string& input,
        std::string_view text)
    {
        expire_suppressed_text();

        std::size_t matched = 0;
        while (matched < text.size() &&
               matched < suppressed_text_input_.size() &&
               text[matched] == suppressed_text_input_[matched]) {
            ++matched;
        }
        if (matched != 0U) {
            suppressed_text_input_.erase(0, matched);
        }
        if (matched < text.size()) {
            suppressed_text_input_.clear();
            input.append(text.substr(matched));
        }
    }

    void set_error(std::string_view context)
    {
        error_.assign(context);
        const char* const detail = SDL_GetError();
        if (detail != nullptr && detail[0] != '\0') {
            error_.append(": ");
            error_.append(detail);
        }
        ready_ = false;
        active_ = false;
    }

    void set_view(DisplayView view)
    {
        if (view_ == view) {
            return;
        }
        view_ = view;
        force_redraw_ = true;
        SDL_SetWindowTitle(
            window_,
            view_ == DisplayView::Terminal
                ? "RISC-V32 CPU Emulator - UART Terminal (F1)"
                : "RISC-V32 CPU Emulator - Linux Framebuffer (F2)");
    }

    void render_terminal()
    {
        std::fill(
            pixels_.begin(),
            pixels_.end(),
            terminal_background);

        for (std::size_t row = 0;
             row < TerminalConsole::rows;
             ++row) {
            for (std::size_t column = 0;
                 column < TerminalConsole::columns;
                 ++column) {
                unsigned char character =
                    static_cast<unsigned char>(
                        terminal_.cell(column, row));
                if (character < 0x20U || character > 0x7FU) {
                    character = static_cast<unsigned char>('?');
                }
                const auto& glyph =
                    font8x8_basic[character - 0x20U];

                for (std::size_t glyph_y = 0;
                     glyph_y < glyph.size();
                     ++glyph_y) {
                    for (std::size_t glyph_x = 0;
                         glyph_x < 8U;
                         ++glyph_x) {
                        if ((glyph[glyph_y] &
                             (std::uint8_t{1} << glyph_x)) == 0U) {
                            continue;
                        }
                        const std::size_t x =
                            column * 8U + glyph_x;
                        const std::size_t y =
                            row * 16U + glyph_y * 2U;
                        pixels_[y * logical_width + x] =
                            terminal_foreground;
                        pixels_[(y + 1U) * logical_width + x] =
                            terminal_foreground;
                    }
                }
            }
        }

        const std::size_t cursor_x =
            terminal_.cursor_column() * 8U;
        const std::size_t cursor_y =
            terminal_.cursor_row() * 16U + 14U;
        for (std::size_t y = cursor_y;
             y < cursor_y + 2U;
             ++y) {
            for (std::size_t x = cursor_x;
                 x < cursor_x + 8U;
                 ++x) {
                pixels_[y * logical_width + x] = terminal_cursor;
            }
        }
    }

    [[nodiscard]] static bool can_upload_directly(
        const devices::Framebuffer* framebuffer) noexcept
    {
        if constexpr (
            std::endian::native != std::endian::little) {
            return false;
        }

        return framebuffer != nullptr &&
               framebuffer->width() ==
                   static_cast<std::uint32_t>(logical_width) &&
               framebuffer->height() ==
                   static_cast<std::uint32_t>(logical_height) &&
               framebuffer->bytes_per_pixel() ==
                   sizeof(std::uint32_t);
    }

    void render_framebuffer(devices::Framebuffer* framebuffer)
    {
        if (framebuffer == nullptr ||
            framebuffer->width() == 0U ||
            framebuffer->height() == 0U) {
            std::fill(pixels_.begin(), pixels_.end(), 0xFF000000U);
            return;
        }

        for (int y = 0; y < logical_height; ++y) {
            const auto source_y = static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(y) *
                framebuffer->height() /
                static_cast<std::uint32_t>(logical_height));
            for (int x = 0; x < logical_width; ++x) {
                const auto source_x = static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(x) *
                    framebuffer->width() /
                    static_cast<std::uint32_t>(logical_width));
                pixels_[static_cast<std::size_t>(y) * logical_width +
                        static_cast<std::size_t>(x)] =
                    read_pixel(*framebuffer, source_x, source_y);
            }
        }
    }

    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    SDL_Texture* texture_{};
    TerminalConsole terminal_;
    std::vector<std::uint32_t> pixels_;
    std::string error_;
    DisplayView view_{DisplayView::Terminal};
    bool owns_sdl_{};
    bool ready_{};
    bool active_{};
    bool force_redraw_{true};
    bool has_presented_{};
    std::uint64_t last_present_ticks_{};
    std::string suppressed_text_input_;
    std::uint64_t last_key_text_ticks_{};
};

SdlFrontend::SdlFrontend()
    : impl_(std::make_unique<Impl>())
{
}

SdlFrontend::~SdlFrontend() = default;

bool SdlFrontend::ready() const noexcept
{
    return impl_->ready();
}

bool SdlFrontend::active() const noexcept
{
    return impl_->active();
}

std::string_view SdlFrontend::error() const noexcept
{
    return impl_->error();
}

DisplayView SdlFrontend::view() const noexcept
{
    return impl_->view();
}

void SdlFrontend::append_uart(std::string_view output)
{
    impl_->append_uart(output);
}

std::string SdlFrontend::poll_input()
{
    return impl_->poll_input();
}

void SdlFrontend::present(devices::Framebuffer* framebuffer)
{
    impl_->present(framebuffer);
}

} // namespace rv32::app
