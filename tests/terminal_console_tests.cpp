#include <iostream>
#include <string>

#include "rv/app/terminal_console.hpp"

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

void test_text_and_control_characters()
{
    rv::app::TerminalConsole terminal;
    terminal.clear_dirty();
    terminal.append("abc\rZ\r\nline\b!");

    CHECK(terminal.cell(0, 0) == 'Z');
    CHECK(terminal.cell(1, 0) == 'b');
    CHECK(terminal.cell(2, 0) == 'c');
    CHECK(terminal.cell(0, 1) == 'l');
    CHECK(terminal.cell(1, 1) == 'i');
    CHECK(terminal.cell(2, 1) == 'n');
    CHECK(terminal.cell(3, 1) == '!');
    CHECK(terminal.cursor_column() == 4U);
    CHECK(terminal.cursor_row() == 1U);
    CHECK(terminal.dirty());
}

void test_ansi_cursor_and_erase_sequences()
{
    rv::app::TerminalConsole terminal;
    terminal.append("discarded");
    terminal.append("\x1b[2J\x1b[2;3HX");

    CHECK(terminal.cell(0, 0) == ' ');
    CHECK(terminal.cell(2, 1) == 'X');
    CHECK(terminal.cursor_column() == 3U);
    CHECK(terminal.cursor_row() == 1U);

    terminal.append("\x1b[1D\x1b[K");
    CHECK(terminal.cell(2, 1) == ' ');
    CHECK(terminal.cursor_column() == 2U);

    terminal.append("\x1b[30;47mA");
    CHECK(terminal.cell(2, 1) == 'A');
}

void test_scrolling_keeps_latest_lines()
{
    rv::app::TerminalConsole terminal;
    terminal.clear();
    for (std::size_t line = 0;
         line < rv::app::TerminalConsole::rows;
         ++line) {
        const char marker =
            static_cast<char>('A' + static_cast<int>(line % 26U));
        terminal.append(std::string(1, marker));
        terminal.append("\r\n");
    }

    CHECK(terminal.cell(0, 0) == 'B');
    CHECK(
        terminal.cell(
            0,
            rv::app::TerminalConsole::rows - 2U) ==
        static_cast<char>(
            'A' +
            static_cast<int>(
                (rv::app::TerminalConsole::rows - 1U) % 26U)));
    CHECK(
        terminal.cursor_row() ==
        rv::app::TerminalConsole::rows - 1U);
}

} // namespace

int main()
{
    test_text_and_control_characters();
    test_ansi_cursor_and_erase_sequences();
    test_scrolling_keeps_latest_lines();

    if (failures == 0) {
        std::cout << "All terminal console tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
