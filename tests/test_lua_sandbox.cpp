/**
 * @file test_lua_sandbox.cpp
 * @brief The Lua sandbox must run text chunks but refuse LuaJIT bytecode.
 *
 * LuaJIT executes unverified bytecode by design, so accepting a bytecode
 * chunk from a mod is a VM escape. executeString / executeFile force
 * mode="t", and the sandboxed load()/loadstring() do the same. This test
 * proves text still works and bytecode is rejected on every entry point.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/scripting/LuaEngine.hpp"

#include <cstdio>
#include <fstream>
#include <string>

namespace {

// A LuaJIT bytecode chunk begins with ESC 'L' 'J' (0x1B 'L' 'J'). Any
// text loader given mode="t" must reject a buffer starting with this.
const std::string kBytecodeHeader = std::string("\x1b\x4c\x4a", 3) +
                                    std::string("\x02\x00", 2);

} // namespace

TEST_CASE("sandbox runs a plain text chunk") {
    aoc::scripting::LuaEngine engine;
    if (!engine.initialize("data/scripts")) {
        // No Lua at build time -> nothing to assert; not a failure.
        return;
    }
    CHECK(engine.executeString("local x = 1 + 1"));
}

TEST_CASE("sandbox rejects a bytecode chunk via executeString") {
    aoc::scripting::LuaEngine engine;
    if (!engine.initialize("data/scripts")) { return; }
    // loadbufferx(mode="t") must refuse the bytecode header rather than
    // execute it: executeString returns false (load error), no crash.
    CHECK_FALSE(engine.executeString(kBytecodeHeader));
}

TEST_CASE("sandbox rejects a bytecode chunk via executeFile") {
    aoc::scripting::LuaEngine engine;
    if (!engine.initialize("data/scripts")) { return; }
    const std::string path = "sandbox_bytecode.luac";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(kBytecodeHeader.data(),
                  static_cast<std::streamsize>(kBytecodeHeader.size()));
    }
    CHECK_FALSE(engine.executeFile(path));
    std::remove(path.c_str());
}

TEST_CASE("sandboxed load() runs text but refuses bytecode") {
    aoc::scripting::LuaEngine engine;
    if (!engine.initialize("data/scripts")) { return; }
    // Text data-mod idiom must still work.
    CHECK(engine.executeString("local t = load('return 42')()"));
    // load() on a bytecode string must not produce a callable chunk.
    CHECK(engine.executeString(
        "local f = load('\\27LJ\\2'); assert(f == nil)"));
}
