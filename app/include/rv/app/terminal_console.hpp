#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace rv::app {

class TerminalConsole {
  public:
    static constexpr std::size_t columns = 80;
    static constexpr std::size_t rows = 30;

    TerminalConsole();

    void append(std::string_view text);
    void clear() noexcept;

    [[nodiscard]] char cell(
        std::size_t column,
        std::size_t row) const noexcept;
    [[nodiscard]] std::span<const char> cells() const noexcept;
    [[nodiscard]] std::size_t cursor_column() const noexcept;
    [[nodiscard]] std::size_t cursor_row() const noexcept;
    [[nodiscard]] bool dirty() const noexcept;
    void clear_dirty() noexcept;

  private:
    enum class ParserState {
        Normal,
        Escape,
        ControlSequence,
    };

    void consume(unsigned char character) noexcept;
    void put_character(char character) noexcept;
    void line_feed() noexcept;
    void scroll() noexcept;
    void begin_control_sequence() noexcept;
    void finish_control_sequence(unsigned char command) noexcept;
    [[nodiscard]] unsigned int parameter(
        std::size_t index,
        unsigned int default_value) const noexcept;

    std::array<char, columns * rows> cells_{};
    std::size_t cursor_column_{};
    std::size_t cursor_row_{};
    ParserState parser_state_{ParserState::Normal};
    std::array<unsigned int, 4> parameters_{};
    std::array<bool, 4> parameter_present_{};
    std::size_t parameter_index_{};
    bool dirty_{true};
};

} // namespace rv::app
