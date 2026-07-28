#include "rv32/app/terminal_console.hpp"

#include <algorithm>

namespace rv32::app {

TerminalConsole::TerminalConsole()
{
    clear();
}

void TerminalConsole::append(std::string_view text)
{
    for (const char character : text) {
        consume(static_cast<unsigned char>(character));
    }
}

void TerminalConsole::clear() noexcept
{
    cells_.fill(' ');
    cursor_column_ = 0;
    cursor_row_ = 0;
    parser_state_ = ParserState::Normal;
    begin_control_sequence();
    dirty_ = true;
}

char TerminalConsole::cell(
    std::size_t column,
    std::size_t row) const noexcept
{
    if (column >= columns || row >= rows) {
        return ' ';
    }
    return cells_[row * columns + column];
}

std::span<const char> TerminalConsole::cells() const noexcept
{
    return cells_;
}

std::size_t TerminalConsole::cursor_column() const noexcept
{
    return cursor_column_;
}

std::size_t TerminalConsole::cursor_row() const noexcept
{
    return cursor_row_;
}

bool TerminalConsole::dirty() const noexcept
{
    return dirty_;
}

void TerminalConsole::clear_dirty() noexcept
{
    dirty_ = false;
}

void TerminalConsole::consume(unsigned char character) noexcept
{
    if (parser_state_ == ParserState::Escape) {
        if (character == '[') {
            begin_control_sequence();
            parser_state_ = ParserState::ControlSequence;
        } else {
            parser_state_ = ParserState::Normal;
        }
        return;
    }

    if (parser_state_ == ParserState::ControlSequence) {
        if (character >= '0' && character <= '9') {
            const unsigned int digit =
                static_cast<unsigned int>(character - '0');
            parameters_[parameter_index_] =
                std::min(
                    9999U,
                    parameters_[parameter_index_] * 10U + digit);
            parameter_present_[parameter_index_] = true;
            return;
        }
        if (character == ';') {
            if (parameter_index_ + 1U < parameters_.size()) {
                ++parameter_index_;
            }
            return;
        }
        if (character == '?' || character == '>') {
            return;
        }
        if (character >= 0x40U && character <= 0x7EU) {
            finish_control_sequence(character);
            parser_state_ = ParserState::Normal;
        }
        return;
    }

    switch (character) {
    case 0x1BU:
        parser_state_ = ParserState::Escape;
        return;
    case '\r':
        cursor_column_ = 0;
        dirty_ = true;
        return;
    case '\n':
    case '\v':
    case '\f':
        line_feed();
        return;
    case '\b':
        if (cursor_column_ != 0U) {
            --cursor_column_;
            dirty_ = true;
        }
        return;
    case '\t': {
        const std::size_t next_tab =
            (cursor_column_ + 8U) & ~std::size_t{7U};
        cursor_column_ = std::min(next_tab, columns - 1U);
        dirty_ = true;
        return;
    }
    default:
        break;
    }

    if (character >= 0x20U && character != 0x7FU) {
        put_character(
            character <= 0x7EU
                ? static_cast<char>(character)
                : '?');
    }
}

void TerminalConsole::put_character(char character) noexcept
{
    cells_[cursor_row_ * columns + cursor_column_] = character;
    ++cursor_column_;
    if (cursor_column_ == columns) {
        cursor_column_ = 0;
        line_feed();
    }
    dirty_ = true;
}

void TerminalConsole::line_feed() noexcept
{
    if (cursor_row_ + 1U == rows) {
        scroll();
    } else {
        ++cursor_row_;
    }
    dirty_ = true;
}

void TerminalConsole::scroll() noexcept
{
    std::move(
        cells_.begin() + static_cast<std::ptrdiff_t>(columns),
        cells_.end(),
        cells_.begin());
    std::fill(cells_.end() - static_cast<std::ptrdiff_t>(columns),
              cells_.end(),
              ' ');
    cursor_row_ = rows - 1U;
}

void TerminalConsole::begin_control_sequence() noexcept
{
    parameters_.fill(0U);
    parameter_present_.fill(false);
    parameter_index_ = 0;
}

unsigned int TerminalConsole::parameter(
    std::size_t index,
    unsigned int default_value) const noexcept
{
    if (index >= parameters_.size() ||
        !parameter_present_[index] ||
        parameters_[index] == 0U) {
        return default_value;
    }
    return parameters_[index];
}

void TerminalConsole::finish_control_sequence(
    unsigned char command) noexcept
{
    const auto clamp_column = [](unsigned int one_based) {
        return std::min<std::size_t>(
            one_based == 0U ? 0U : one_based - 1U,
            columns - 1U);
    };
    const auto clamp_row = [](unsigned int one_based) {
        return std::min<std::size_t>(
            one_based == 0U ? 0U : one_based - 1U,
            rows - 1U);
    };

    switch (command) {
    case 'A':
        cursor_row_ -= std::min<std::size_t>(
            cursor_row_, parameter(0, 1U));
        break;
    case 'B':
        cursor_row_ = std::min<std::size_t>(
            rows - 1U,
            cursor_row_ + parameter(0, 1U));
        break;
    case 'C':
        cursor_column_ = std::min<std::size_t>(
            columns - 1U,
            cursor_column_ + parameter(0, 1U));
        break;
    case 'D':
        cursor_column_ -= std::min<std::size_t>(
            cursor_column_, parameter(0, 1U));
        break;
    case 'G':
        cursor_column_ = clamp_column(parameter(0, 1U));
        break;
    case 'd':
        cursor_row_ = clamp_row(parameter(0, 1U));
        break;
    case 'H':
    case 'f':
        cursor_row_ = clamp_row(parameter(0, 1U));
        cursor_column_ = clamp_column(parameter(1, 1U));
        break;
    case 'J': {
        const unsigned int mode =
            parameter_present_[0] ? parameters_[0] : 0U;
        const std::size_t cursor =
            cursor_row_ * columns + cursor_column_;
        if (mode == 2U || mode == 3U) {
            cells_.fill(' ');
            cursor_column_ = 0;
            cursor_row_ = 0;
        } else if (mode == 1U) {
            std::fill(cells_.begin(), cells_.begin() +
                          static_cast<std::ptrdiff_t>(cursor + 1U), ' ');
        } else {
            std::fill(cells_.begin() +
                          static_cast<std::ptrdiff_t>(cursor),
                      cells_.end(), ' ');
        }
        break;
    }
    case 'K': {
        const unsigned int mode =
            parameter_present_[0] ? parameters_[0] : 0U;
        const auto line_begin =
            cells_.begin() +
            static_cast<std::ptrdiff_t>(cursor_row_ * columns);
        const auto cursor =
            line_begin + static_cast<std::ptrdiff_t>(cursor_column_);
        const auto line_end =
            line_begin + static_cast<std::ptrdiff_t>(columns);
        if (mode == 2U) {
            std::fill(line_begin, line_end, ' ');
        } else if (mode == 1U) {
            std::fill(line_begin, cursor + 1, ' ');
        } else {
            std::fill(cursor, line_end, ' ');
        }
        break;
    }
    case 'm':
        // Colour attributes do not affect the minimal monochrome terminal.
        break;
    default:
        break;
    }
    dirty_ = true;
}

} // namespace rv32::app
