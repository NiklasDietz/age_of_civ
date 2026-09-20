#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/debug/DebugServer.hpp"

#include <string>

namespace {

std::string ok(const std::unordered_map<std::string, std::string>&, const std::string&) {
    return "{}";
}

} // namespace

TEST_CASE("routesJson reports nothing before a route is registered") {
    aoc::debug::DebugServer server;
    CHECK(server.routesJson() == "[]");
}

TEST_CASE("routesJson reports every registered route, with its verb and query spec") {
    aoc::debug::DebugServer server;
    server.routeJson(aoc::debug::DebugServer::Method::Get, "/ping", ok);
    server.routeJson(aoc::debug::DebugServer::Method::Post, "/game/unit/move", ok, "player=&q=&r=&targetQ=&targetR=");

    CHECK(server.routesJson() ==
          "[{\"method\":\"GET\",\"path\":\"/ping\"},"
          "{\"method\":\"POST\",\"path\":\"/game/unit/move"
          "?player=&q=&r=&targetQ=&targetR=\"}]");
}

TEST_CASE("a route registered without a spec advertises no query string") {
    aoc::debug::DebugServer server;
    server.routeJson(aoc::debug::DebugServer::Method::Get, "/info", ok, "");
    CHECK(server.routesJson() == "[{\"method\":\"GET\",\"path\":\"/info\"}]");
}

TEST_CASE("the catalogue keeps registration order, so /schema reads like the source") {
    aoc::debug::DebugServer server;
    server.routeJson(aoc::debug::DebugServer::Method::Get, "/b", ok);
    server.routeJson(aoc::debug::DebugServer::Method::Get, "/a", ok);
    const std::string json = server.routesJson();
    CHECK(json.find("/b") < json.find("/a"));
}
