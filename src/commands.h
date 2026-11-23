// Text commands. The same words work from the phone (BLE Command
// characteristic) and from USB serial.
#pragma once
#include <ctype.h>
#include <stdint.h>
#include <string.h>

enum class Command : uint8_t { None, Cancel, Ack, Test, Status };

// "cancel", "ack", "test", "status", or just their first letter.
// Case and trailing spaces/newlines are ignored.
inline Command parseCommand(const char* text) {
  char word[16] = {0};
  size_t n = 0;
  for (; text[n] && n < sizeof(word) - 1; n++) word[n] = tolower((unsigned char)text[n]);
  while (n && isspace((unsigned char)word[n - 1])) word[--n] = 0;

  static const struct {
    const char* name;
    Command command;
  } kCommands[] = {{"cancel", Command::Cancel}, {"ack", Command::Ack}, {"test", Command::Test},
                   {"status", Command::Status}};
  for (const auto& c : kCommands)
    if (!strcmp(word, c.name) || (n == 1 && word[0] == c.name[0])) return c.command;
  return Command::None;
}
