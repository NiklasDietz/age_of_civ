/**
 * @file test_screen_registry.cpp
 * @brief `ScreenRegistry::anyOpen` / `onlyOpen` against stub screens. No
 *        `UIManager` instance is needed, but ScreenRegistry.cpp is a gfx-only
 *        source, so the test is registered only when NOT AOC_HEADLESS.
 *        `closeAll` is covered by the live Esc-to-close check, not here.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/ui/IScreen.hpp"
#include "aoc/ui/ScreenRegistry.hpp"

using aoc::ui::IScreen;
using aoc::ui::ScreenRegistry;
using aoc::ui::UIManager;

namespace {

class StubScreen final : public IScreen {
public:
    explicit StubScreen(bool open) : m_open(open) {}
    [[nodiscard]] bool isOpen() const override { return this->m_open; }
    void close(UIManager& /*ui*/) override { this->m_open = false; }
    void onResize(UIManager& /*ui*/, float /*width*/, float /*height*/) override {}
    void setOpen(bool open) { this->m_open = open; }

private:
    bool m_open;
};

} // namespace

TEST_CASE("anyOpen: empty registry reports nothing open") {
    const ScreenRegistry registry{};
    CHECK_FALSE(registry.anyOpen());
    CHECK(registry.size() == 0);
}

TEST_CASE("anyOpen: follows the open state of registered screens") {
    ScreenRegistry registry;
    StubScreen a(false);
    StubScreen b(false);
    registry.add(&a);
    registry.add(&b);
    CHECK_FALSE(registry.anyOpen());
    b.setOpen(true);
    CHECK(registry.anyOpen());
    b.setOpen(false);
    CHECK_FALSE(registry.anyOpen());
}

TEST_CASE("onlyOpen: true only while the target is the sole open screen") {
    ScreenRegistry registry;
    StubScreen panel(false);
    StubScreen other(false);
    registry.add(&panel);
    registry.add(&other);

    CHECK_FALSE(registry.onlyOpen(&panel)); // target closed
    panel.setOpen(true);
    CHECK(registry.onlyOpen(&panel)); // alone
    other.setOpen(true);
    CHECK_FALSE(registry.onlyOpen(&panel)); // a second screen is open
    other.setOpen(false);
    CHECK(registry.onlyOpen(&panel)); // back to alone
}

TEST_CASE("onlyOpen: null or unregistered screens are never the only open one") {
    ScreenRegistry registry;
    StubScreen registered(false);
    StubScreen stranger(true);
    registry.add(&registered);

    CHECK_FALSE(registry.onlyOpen(nullptr));
    CHECK_FALSE(registry.onlyOpen(&stranger));
    // An open stranger does not count against a registered screen either.
    registered.setOpen(true);
    CHECK(registry.onlyOpen(&registered));
}

TEST_CASE("add: duplicates and null are rejected; remove clears onlyOpen") {
    ScreenRegistry registry;
    StubScreen panel(true);
    registry.add(&panel);
    registry.add(&panel);
    registry.add(nullptr);
    CHECK(registry.size() == 1);
    CHECK(registry.onlyOpen(&panel));
    registry.remove(&panel);
    CHECK(registry.size() == 0);
    CHECK_FALSE(registry.onlyOpen(&panel));
}
