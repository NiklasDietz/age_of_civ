/**
 * @file ScreenRegistry.cpp
 */

#include "aoc/ui/ScreenRegistry.hpp"
#include "aoc/ui/IScreen.hpp"

#include <algorithm>

namespace aoc::ui {

void ScreenRegistry::add(IScreen* screen) {
    if (screen == nullptr) { return; }
    if (std::find(this->m_screens.begin(), this->m_screens.end(), screen)
        != this->m_screens.end()) {
        return;
    }
    this->m_screens.push_back(screen);
}

void ScreenRegistry::remove(IScreen* screen) {
    this->m_screens.erase(
        std::remove(this->m_screens.begin(), this->m_screens.end(), screen),
        this->m_screens.end());
}

bool ScreenRegistry::anyOpen() const {
    for (const IScreen* s : this->m_screens) {
        if (s != nullptr && s->isOpen()) { return true; }
    }
    return false;
}

void ScreenRegistry::closeAll(UIManager& ui) {
    for (IScreen* s : this->m_screens) {
        if (s != nullptr && s->isOpen()) {
            s->close(ui);
        }
    }
}

void ScreenRegistry::onResize(UIManager& ui, float width, float height) {
    for (IScreen* s : this->m_screens) {
        if (s != nullptr) {
            s->onResize(ui, width, height);
        }
    }
}

void ScreenRegistry::pushModal(IScreen* screen) {
    if (screen == nullptr) { return; }
    this->m_modalStack.push_back(screen);
}

IScreen* ScreenRegistry::popModal() {
    if (this->m_modalStack.empty()) { return nullptr; }
    IScreen* top = this->m_modalStack.back();
    this->m_modalStack.pop_back();
    return top;
}

IScreen* ScreenRegistry::topModal() const {
    if (this->m_modalStack.empty()) { return nullptr; }
    return this->m_modalStack.back();
}

void ScreenRegistry::closeModalStack(UIManager& ui) {
    // Pop + close in reverse order so newest modal closes first, in
    // case close handlers depend on the stack being consistent.
    while (!this->m_modalStack.empty()) {
        IScreen* top = this->m_modalStack.back();
        this->m_modalStack.pop_back();
        if (top != nullptr && top->isOpen()) {
            top->close(ui);
        }
    }
}

} // namespace aoc::ui
