#include <emscripten/bind.h>
#include <sstream>
#include <string>

import parser;

void parse_replay(const std::string& data) {
    std::stringstream ss(data);
    wrpl::process_stream(ss);
}

EMSCRIPTEN_BINDINGS(wrpl_module) {
    emscripten::function("parseReplay", &parse_replay);
}
