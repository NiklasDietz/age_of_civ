/**
 * @file UIManager.cpp
 * @brief Widget tree management, layout, input, and rendering.
 */

#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/BitmapFont.hpp"
#include "aoc/ui/IconAtlas.hpp"
#include "aoc/core/Log.hpp"

#include <renderer/Renderer2D.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>

namespace aoc::ui {

// ============================================================================
// Widget creation
// ============================================================================

WidgetId UIManager::allocateWidget() {
    WidgetId id;
    if (!this->m_freeList.empty()) {
        id = this->m_freeList.back();
        this->m_freeList.pop_back();
        this->m_widgets[id] = Widget{};
    } else {
        id = this->m_nextId++;
        this->m_widgets.emplace_back();
    }
    this->m_widgets[id].id = id;
    return id;
}

WidgetId UIManager::createPanel(Rect bounds, PanelData panelData) {
    WidgetId id = this->allocateWidget();
    Widget& w = this->m_widgets[id];
    w.requestedBounds = bounds;
    w.data = std::move(panelData);
    this->m_rootWidgets.push_back(id);
    return id;
}

WidgetId UIManager::createPanel(WidgetId parent, Rect bounds, PanelData panelData) {
    assert(parent < this->m_widgets.size());
    WidgetId id = this->allocateWidget();
    Widget& w = this->m_widgets[id];
    w.requestedBounds = bounds;
    w.data = std::move(panelData);
    w.parent = parent;
    this->m_widgets[parent].children.push_back(id);
    return id;
}

WidgetId UIManager::createButton(WidgetId parent, Rect bounds, ButtonData buttonData) {
    assert(parent < this->m_widgets.size());
    WidgetId id = this->allocateWidget();
    Widget& w = this->m_widgets[id];
    w.requestedBounds = bounds;
    w.data = std::move(buttonData);
    w.parent = parent;
    this->m_widgets[parent].children.push_back(id);
    return id;
}

WidgetId UIManager::createLabel(WidgetId parent, Rect bounds, LabelData labelData) {
    assert(parent < this->m_widgets.size());
    WidgetId id = this->allocateWidget();
    Widget& w = this->m_widgets[id];
    w.requestedBounds = bounds;
    w.data = std::move(labelData);
    w.parent = parent;
    this->m_widgets[parent].children.push_back(id);
    return id;
}

WidgetId UIManager::createScrollList(WidgetId parent, Rect bounds, ScrollListData data) {
    assert(parent < this->m_widgets.size());
    WidgetId id = this->allocateWidget();
    Widget& w = this->m_widgets[id];
    w.requestedBounds = bounds;
    w.data = std::move(data);
    w.parent = parent;
    this->m_widgets[parent].children.push_back(id);
    return id;
}

WidgetId UIManager::createTabBar(WidgetId parent, Rect bounds, TabBarData data) {
    assert(parent < this->m_widgets.size());
    WidgetId id = this->allocateWidget();
    Widget& w = this->m_widgets[id];
    w.requestedBounds = bounds;
    w.data = std::move(data);
    w.parent = parent;
    this->m_widgets[parent].children.push_back(id);
    return id;
}

WidgetId UIManager::createProgressBar(WidgetId parent, Rect bounds, ProgressBarData data) {
    assert(parent < this->m_widgets.size());
    WidgetId id = this->allocateWidget();
    Widget& w = this->m_widgets[id];
    w.requestedBounds = bounds;
    w.data = std::move(data);
    w.parent = parent;
    this->m_widgets[parent].children.push_back(id);
    return id;
}

WidgetId UIManager::createSlider(WidgetId parent, Rect bounds, SliderData data) {
    assert(parent < this->m_widgets.size());
    WidgetId id = this->allocateWidget();
    Widget& w = this->m_widgets[id];
    w.requestedBounds = bounds;
    w.data = std::move(data);
    w.parent = parent;
    this->m_widgets[parent].children.push_back(id);
    return id;
}

WidgetId UIManager::createIcon(WidgetId parent, Rect bounds, IconData data) {
    assert(parent < this->m_widgets.size());
    WidgetId id = this->allocateWidget();
    Widget& w = this->m_widgets[id];
    w.requestedBounds = bounds;
    w.data = std::move(data);
    w.parent = parent;
    this->m_widgets[parent].children.push_back(id);
    return id;
}

WidgetId UIManager::createRichText(WidgetId parent, Rect bounds, RichTextData data) {
    assert(parent < this->m_widgets.size());
    WidgetId id = this->allocateWidget();
    Widget& w = this->m_widgets[id];
    w.requestedBounds = bounds;
    w.data = std::move(data);
    w.parent = parent;
    this->m_widgets[parent].children.push_back(id);
    return id;
}

WidgetId UIManager::createPortrait(WidgetId parent, Rect bounds, PortraitData data) {
    assert(parent < this->m_widgets.size());
    WidgetId id = this->allocateWidget();
    Widget& w = this->m_widgets[id];
    w.requestedBounds = bounds;
    w.data = std::move(data);
    w.parent = parent;
    this->m_widgets[parent].children.push_back(id);
    return id;
}

WidgetId UIManager::createMarkdown(WidgetId parent, Rect bounds, MarkdownData data) {
    assert(parent < this->m_widgets.size());
    WidgetId id = this->allocateWidget();
    Widget& w = this->m_widgets[id];
    w.requestedBounds = bounds;
    w.data = std::move(data);
    w.parent = parent;
    this->m_widgets[parent].children.push_back(id);
    return id;
}

WidgetId UIManager::createListRow(WidgetId parent, Rect bounds, ListRowData data) {
    assert(parent < this->m_widgets.size());
    WidgetId id = this->allocateWidget();
    Widget& w = this->m_widgets[id];
    w.requestedBounds = bounds;
    w.data = std::move(data);
    w.parent = parent;
    w.selectable = true;
    this->m_widgets[parent].children.push_back(id);
    return id;
}

// --------------------------------------------------------------------------
// Animation, events, drag, multi-select, network, audio
// --------------------------------------------------------------------------

bool UIManager::activateShortcut(int32_t key) {
    bool fired = false;
    for (Widget& w : this->m_widgets) {
        if (w.id == INVALID_WIDGET || !w.isVisible) { continue; }
        if (ButtonData* btn = std::get_if<ButtonData>(&w.data)) {
            if (btn->shortcut == key && btn->onClick) {
                btn->onClick();
                this->logEvent({w.id, "shortcut", this->m_clockSec});
                if (btn->clickSound != 0) {
                    this->m_audioOutbox.push_back(btn->clickSound);
                }
                fired = true;
            }
        }
    }
    return fired;
}

void UIManager::logEvent(WidgetEvent ev) {
    if (this->m_eventLog.size() >= MAX_EVENT_LOG) {
        this->m_eventLog.erase(this->m_eventLog.begin());
    }
    this->m_eventLog.push_back(ev);
}

void UIManager::tweenAlpha(WidgetId id, float targetAlpha, float seconds) {
    Widget* w = this->getWidget(id);
    if (w == nullptr) { return; }
    w->alphaTarget    = targetAlpha;
    w->alphaTweenSec  = seconds;
    w->alphaTweenLeft = seconds;
}

void UIManager::flash(WidgetId id, Color tint, float durationSec) {
    Widget* w = this->getWidget(id);
    if (w == nullptr) { return; }
    w->flashColor         = tint;
    w->flashDurationLeft  = durationSec;
    w->flashDurationTotal = durationSec;
}

void UIManager::tickAnimations(float deltaSec) {
    this->m_clockSec += deltaSec;
    for (Widget& w : this->m_widgets) {
        if (w.id == INVALID_WIDGET) { continue; }
        // TabBar underline slide: ease `activeTabAnim` toward
        // `activeTab`. 8x/s critically-damped lerp feels snappy.
        if (TabBarData* tabs = std::get_if<TabBarData>(&w.data)) {
            const float target = static_cast<float>(tabs->activeTab);
            const float lerp = std::min(1.0f, deltaSec * 12.0f);
            tabs->activeTabAnim += (target - tabs->activeTabAnim) * lerp;
        }
        // Alpha tween.
        if (w.alphaTweenLeft > 0.0f) {
            w.alphaTweenLeft -= deltaSec;
            if (w.alphaTweenLeft <= 0.0f) {
                w.alpha = w.alphaTarget;
                w.alphaTweenLeft = 0.0f;
            } else {
                const float t = 1.0f - (w.alphaTweenLeft / w.alphaTweenSec);
                w.alpha = w.alpha + (w.alphaTarget - w.alpha) * t;
            }
        }
        // Hover scale: ease toward 1 + (hoverScale-1) * isHovered.
        if (w.hoverScale != 1.0f) {
            const float target = w.isHovered ? w.hoverScale : 1.0f;
            const float lerp = std::min(1.0f, deltaSec * 12.0f);
            w.currentScale += (target - w.currentScale) * lerp;
        }
        // Flash decay.
        if (w.flashDurationLeft > 0.0f) {
            w.flashDurationLeft -= deltaSec;
            if (w.flashDurationLeft < 0.0f) { w.flashDurationLeft = 0.0f; }
        }
    }
    // Held-button repeat.
    if (this->m_pressedWidget != INVALID_WIDGET) {
        Widget* held = this->getWidget(this->m_pressedWidget);
        if (held != nullptr) {
            if (ButtonData* btn = std::get_if<ButtonData>(&held->data)) {
                if (btn->repeatRateHz > 0.0f && btn->onClick) {
                    ButtonRepeatState& st = this->m_repeatStates[this->m_pressedWidget];
                    st.waited += deltaSec;
                    if (!st.armed) {
                        if (st.waited >= btn->repeatDelaySec) {
                            st.armed = true;
                            st.waited = 0.0f;
                        }
                    } else {
                        const float interval = 1.0f / btn->repeatRateHz;
                        while (st.waited >= interval) {
                            btn->onClick();
                            st.waited -= interval;
                        }
                    }
                }
            }
        }
    } else {
        this->m_repeatStates.clear();
    }
}

void UIManager::onDrop(WidgetId target, std::function<void(uint32_t)> handler) {
    if (!handler) {
        this->m_dropHandlers.erase(target);
        return;
    }
    this->m_dropHandlers[target] = std::move(handler);
}

void UIManager::selectOnly(WidgetId id) {
    Widget* w = this->getWidget(id);
    if (w == nullptr) { return; }
    const WidgetId parent = w->parent;
    for (WidgetId sib : (parent != INVALID_WIDGET) ? this->m_widgets[parent].children
                                                    : this->m_rootWidgets) {
        Widget* sw = this->getWidget(sib);
        if (sw != nullptr && sw->selectable) { sw->isSelected = false; }
    }
    w->isSelected = true;
    this->m_selectAnchor = id;
}

void UIManager::selectToggle(WidgetId id) {
    Widget* w = this->getWidget(id);
    if (w == nullptr) { return; }
    w->isSelected = !w->isSelected;
    this->m_selectAnchor = id;
}

void UIManager::selectRangeTo(WidgetId id) {
    Widget* w = this->getWidget(id);
    if (w == nullptr || this->m_selectAnchor == INVALID_WIDGET) {
        this->selectOnly(id);
        return;
    }
    Widget* anchor = this->getWidget(this->m_selectAnchor);
    if (anchor == nullptr || anchor->parent != w->parent) {
        this->selectOnly(id);
        return;
    }
    const int32_t a = std::min(anchor->selectIndex, w->selectIndex);
    const int32_t b = std::max(anchor->selectIndex, w->selectIndex);
    const std::vector<WidgetId>& siblings = (w->parent != INVALID_WIDGET)
        ? this->m_widgets[w->parent].children : this->m_rootWidgets;
    for (WidgetId sib : siblings) {
        Widget* sw = this->getWidget(sib);
        if (sw == nullptr || !sw->selectable) { continue; }
        sw->isSelected = (sw->selectIndex >= a && sw->selectIndex <= b);
    }
}

std::vector<WidgetId> UIManager::currentSelection(WidgetId parent) const {
    std::vector<WidgetId> out;
    const std::vector<WidgetId>& siblings = (parent != INVALID_WIDGET
                                              && parent < this->m_widgets.size())
        ? this->m_widgets[parent].children : this->m_rootWidgets;
    for (WidgetId sib : siblings) {
        const Widget* sw = this->getWidget(sib);
        if (sw != nullptr && sw->selectable && sw->isSelected) { out.push_back(sib); }
    }
    return out;
}

void UIManager::emitNetworkEvent(NetworkEvent ev) {
    this->m_netOutbox.push_back(std::move(ev));
}

void UIManager::scrollWidget(WidgetId id, float delta) {
    Widget* w = this->getWidget(id);
    if (w == nullptr) {
        return;
    }
    ScrollListData* scrollData = std::get_if<ScrollListData>(&w->data);
    if (scrollData == nullptr) {
        return;
    }
    scrollData->scrollOffset += delta;
    // Clamp to valid range
    float maxScroll = scrollData->contentHeight - w->computedBounds.h;
    if (maxScroll < 0.0f) {
        maxScroll = 0.0f;
    }
    if (scrollData->scrollOffset < 0.0f) {
        scrollData->scrollOffset = 0.0f;
    }
    if (scrollData->scrollOffset > maxScroll) {
        scrollData->scrollOffset = maxScroll;
    }
}

void UIManager::removeWidget(WidgetId id) {
    if (id >= this->m_widgets.size() || this->m_widgets[id].id == INVALID_WIDGET) {
        return;
    }

    // Recursively remove children
    Widget& w = this->m_widgets[id];
    for (WidgetId child : w.children) {
        this->removeWidget(child);
    }

    // Remove from parent's children list
    if (w.parent != INVALID_WIDGET && w.parent < this->m_widgets.size()) {
        std::vector<WidgetId>& siblings = this->m_widgets[w.parent].children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
    }

    // Remove from root list if applicable
    this->m_rootWidgets.erase(
        std::remove(this->m_rootWidgets.begin(), this->m_rootWidgets.end(), id),
        this->m_rootWidgets.end());

    // Drop any declarative bindings tied to this widget so a later
    // `updateBindings` call doesn't hit a dead slot.
    this->clearBindings(id);

    // Bump generation so any outstanding WidgetHandle for this slot
    // compares unequal after the slot is reused.
    if (id < this->m_generations.size()) {
        ++this->m_generations[id];
    } else {
        this->m_generations.resize(id + 1, 0);
        ++this->m_generations[id];
    }

    w.id = INVALID_WIDGET;
    this->m_freeList.push_back(id);
}

WidgetHandle UIManager::toHandle(WidgetId id) const {
    WidgetHandle h;
    h.id = id;
    h.generation = (id < this->m_generations.size()) ? this->m_generations[id] : 0;
    return h;
}

bool UIManager::isLive(WidgetHandle handle) const {
    if (handle.id == INVALID_WIDGET) { return false; }
    if (handle.id >= this->m_widgets.size()) { return false; }
    if (this->m_widgets[handle.id].id == INVALID_WIDGET) { return false; }
    const uint32_t gen = (handle.id < this->m_generations.size())
        ? this->m_generations[handle.id] : 0;
    return gen == handle.generation;
}

// ============================================================================
// Widget access
// ============================================================================

Widget* UIManager::getWidget(WidgetId id) {
    if (id >= this->m_widgets.size() || this->m_widgets[id].id == INVALID_WIDGET) {
        return nullptr;
    }
    return &this->m_widgets[id];
}

const Widget* UIManager::getWidget(WidgetId id) const {
    if (id >= this->m_widgets.size() || this->m_widgets[id].id == INVALID_WIDGET) {
        return nullptr;
    }
    return &this->m_widgets[id];
}

void UIManager::setLabelText(WidgetId id, std::string text) {
    Widget* w = this->getWidget(id);
    if (w == nullptr) {
        return;
    }
    if (LabelData* label = std::get_if<LabelData>(&w->data)) {
        label->text = std::move(text);
    }
}

void UIManager::setButtonLabel(WidgetId id, std::string label) {
    Widget* w = this->getWidget(id);
    if (w == nullptr) {
        return;
    }
    if (ButtonData* btn = std::get_if<ButtonData>(&w->data)) {
        btn->label = std::move(label);
    }
}

void UIManager::setVisible(WidgetId id, bool visible) {
    Widget* w = this->getWidget(id);
    if (w != nullptr) {
        w->isVisible = visible;
    }
}

void UIManager::setWidgetTooltip(WidgetId id, std::string text) {
    Widget* w = this->getWidget(id);
    if (w != nullptr) {
        w->tooltip = std::move(text);
    }
}

std::string_view UIManager::widgetTooltip(WidgetId id) const {
    const Widget* w = this->getWidget(id);
    if (w == nullptr) { return {}; }
    return w->tooltip;
}

// --------------------------------------------------------------------------
// Bindings
// --------------------------------------------------------------------------

void UIManager::bindLabel(WidgetId id, std::function<std::string()> supplier) {
    if (!supplier) {
        this->m_labelBindings.erase(id);
        return;
    }
    this->m_labelBindings[id] = std::move(supplier);
}

void UIManager::bindButtonLabel(WidgetId id, std::function<std::string()> supplier) {
    if (!supplier) {
        this->m_buttonBindings.erase(id);
        return;
    }
    this->m_buttonBindings[id] = std::move(supplier);
}

void UIManager::bindVisibility(WidgetId id, std::function<bool()> supplier) {
    if (!supplier) {
        this->m_visibilityBindings.erase(id);
        return;
    }
    this->m_visibilityBindings[id] = std::move(supplier);
}

void UIManager::clearBindings(WidgetId id) {
    this->m_labelBindings.erase(id);
    this->m_buttonBindings.erase(id);
    this->m_visibilityBindings.erase(id);
}

// --------------------------------------------------------------------------
// Keyboard focus
// --------------------------------------------------------------------------

namespace {

// Walk live widgets linearly. Returns in-id order; focus cycles wrap.
std::vector<WidgetId> collectFocusable(const std::vector<Widget>& widgets) {
    std::vector<WidgetId> out;
    out.reserve(widgets.size());
    for (const Widget& w : widgets) {
        if (w.id == INVALID_WIDGET) { continue; }
        if (!w.isVisible) { continue; }
        if (!w.focusable) { continue; }
        out.push_back(w.id);
    }
    return out;
}

} // namespace

WidgetId UIManager::focusNext() {
    std::vector<WidgetId> list = collectFocusable(this->m_widgets);
    if (list.empty()) {
        this->m_focusedWidget = INVALID_WIDGET;
        return INVALID_WIDGET;
    }
    // Mark previous focused false.
    if (this->m_focusedWidget != INVALID_WIDGET) {
        Widget* prev = this->getWidget(this->m_focusedWidget);
        if (prev != nullptr) { prev->isFocused = false; }
    }
    auto it = std::find(list.begin(), list.end(), this->m_focusedWidget);
    WidgetId next = (it == list.end() || std::next(it) == list.end())
        ? list.front() : *std::next(it);
    this->m_focusedWidget = next;
    Widget* nw = this->getWidget(next);
    if (nw != nullptr) { nw->isFocused = true; }
    return next;
}

WidgetId UIManager::focusPrev() {
    std::vector<WidgetId> list = collectFocusable(this->m_widgets);
    if (list.empty()) {
        this->m_focusedWidget = INVALID_WIDGET;
        return INVALID_WIDGET;
    }
    if (this->m_focusedWidget != INVALID_WIDGET) {
        Widget* prev = this->getWidget(this->m_focusedWidget);
        if (prev != nullptr) { prev->isFocused = false; }
    }
    auto it = std::find(list.begin(), list.end(), this->m_focusedWidget);
    WidgetId prevId;
    if (it == list.end() || it == list.begin()) {
        prevId = list.back();
    } else {
        prevId = *std::prev(it);
    }
    this->m_focusedWidget = prevId;
    Widget* nw = this->getWidget(prevId);
    if (nw != nullptr) { nw->isFocused = true; }
    return prevId;
}

void UIManager::activateFocused() {
    if (this->m_focusedWidget == INVALID_WIDGET) { return; }
    Widget* w = this->getWidget(this->m_focusedWidget);
    if (w == nullptr) { return; }
    if (ButtonData* btn = std::get_if<ButtonData>(&w->data)) {
        if (btn->onClick) { btn->onClick(); }
    }
}

std::string UIManager::dumpTreeJson() const {
    // Emit a simple JSON-like array. Not strict JSON (no escaping of
    // label text for brevity); intended for developer inspector use,
    // not machine parsing.
    std::string out;
    out.reserve(this->m_widgets.size() * 80);
    out += "[\n";
    bool first = true;
    for (const Widget& w : this->m_widgets) {
        if (w.id == INVALID_WIDGET) { continue; }
        if (!first) { out += ",\n"; }
        first = false;
        const char* kind = "panel";
        if (std::holds_alternative<ButtonData>(w.data))      { kind = "button"; }
        else if (std::holds_alternative<LabelData>(w.data))       { kind = "label"; }
        else if (std::holds_alternative<ScrollListData>(w.data))  { kind = "scroll"; }
        else if (std::holds_alternative<TabBarData>(w.data))      { kind = "tabs"; }
        else if (std::holds_alternative<ProgressBarData>(w.data)) { kind = "progress"; }
        else if (std::holds_alternative<SliderData>(w.data))      { kind = "slider"; }
        else if (std::holds_alternative<IconData>(w.data))        { kind = "icon"; }
        out += "  {\"id\":" + std::to_string(w.id);
        out += ",\"kind\":\"" + std::string(kind) + "\"";
        out += ",\"parent\":" + std::to_string(w.parent);
        out += ",\"x\":" + std::to_string(static_cast<int32_t>(w.computedBounds.x));
        out += ",\"y\":" + std::to_string(static_cast<int32_t>(w.computedBounds.y));
        out += ",\"w\":" + std::to_string(static_cast<int32_t>(w.computedBounds.w));
        out += ",\"h\":" + std::to_string(static_cast<int32_t>(w.computedBounds.h));
        out += ",\"visible\":" + std::string(w.isVisible ? "true" : "false");
        out += ",\"focus\":" + std::string(w.isFocused ? "true" : "false");
        out += "}";
    }
    out += "\n]\n";
    return out;
}

void UIManager::updateBindings() {
    // Labels: set if changed. The setLabelText path is cheap (string
    // move + repaint only if different) so we don't need local caching.
    for (auto& [id, supplier] : this->m_labelBindings) {
        this->setLabelText(id, supplier());
    }
    for (auto& [id, supplier] : this->m_buttonBindings) {
        this->setButtonLabel(id, supplier());
    }
    for (auto& [id, supplier] : this->m_visibilityBindings) {
        this->setVisible(id, supplier());
    }
}

// ============================================================================
// Input
// ============================================================================

bool UIManager::handleInput(float mouseX, float mouseY,
                             bool mousePressed, bool mouseReleased,
                             float scrollDelta,
                             bool rightPressed, bool rightReleased) {
    // Panel drag pass: if the pressed widget is flagged draggable and
    // the button is still held, translate it under the cursor.
    // Mirrors the slider pass so both interactions work the same way.
    if (this->m_pressedWidget != INVALID_WIDGET) {
        Widget* pressed = this->getWidget(this->m_pressedWidget);
        if (pressed != nullptr && pressed->isDraggable) {
            pressed->requestedBounds.x = mouseX - pressed->dragAnchorX;
            pressed->requestedBounds.y = mouseY - pressed->dragAnchorY;
        }
    }

    // Slider drag pass: if the pressed widget is a slider and the
    // button is still held, continuously update value from mouseX.
    // This runs before hover / hit-test so the slider keeps tracking
    // the cursor even when dragged outside its own bounds.
    if (this->m_pressedWidget != INVALID_WIDGET) {
        Widget* pressed = this->getWidget(this->m_pressedWidget);
        if (pressed != nullptr) {
            if (SliderData* slider = std::get_if<SliderData>(&pressed->data)) {
                const float minX = pressed->computedBounds.x;
                const float maxX = pressed->computedBounds.x + pressed->computedBounds.w;
                if (maxX > minX) {
                    float t = (mouseX - minX) / (maxX - minX);
                    if (t < 0.0f) { t = 0.0f; }
                    if (t > 1.0f) { t = 1.0f; }
                    float v = slider->minValue
                        + t * (slider->maxValue - slider->minValue);
                    if (slider->step > 0.0f) {
                        v = slider->minValue
                            + std::round((v - slider->minValue) / slider->step)
                              * slider->step;
                    }
                    if (v != slider->value) {
                        slider->value = v;
                        if (slider->onValueChanged) {
                            slider->onValueChanged(v);
                        }
                    }
                }
            }
        }
    }

    // Reset hover state
    if (this->m_hoveredWidget != INVALID_WIDGET && this->m_hoveredWidget < this->m_widgets.size()) {
        this->m_widgets[this->m_hoveredWidget].isHovered = false;
    }

    // Hit test
    WidgetId hit = this->hitTest(mouseX, mouseY);
    this->m_hoveredWidget = hit;

    if (hit != INVALID_WIDGET) {
        Widget& w = this->m_widgets[hit];
        w.isHovered = true;

        // Scroll wheel: walk up from hovered widget to find a ScrollList ancestor.
        // Any scroll over a UI widget is consumed (prevents camera zoom while in menus).
        if (scrollDelta != 0.0f) {
            constexpr float SCROLL_PIXEL_MULTIPLIER = 20.0f;
            WidgetId cur = hit;
            while (cur != INVALID_WIDGET) {
                Widget* candidate = this->getWidget(cur);
                if (candidate != nullptr && std::holds_alternative<ScrollListData>(candidate->data)) {
                    this->scrollWidget(cur, -scrollDelta * SCROLL_PIXEL_MULTIPLIER);
                    break;
                }
                cur = (candidate != nullptr) ? candidate->parent : INVALID_WIDGET;
            }
            return true;  // Consume scroll even if no scroll list found (hovering any UI)
        }

        if (mousePressed) {
            w.isPressed = true;
            this->m_pressedWidget = hit;
            // Capture cursor offset relative to the widget origin so the
            // drag translation stays smooth across frames.
            if (w.isDraggable) {
                w.dragAnchorX = mouseX - w.requestedBounds.x;
                w.dragAnchorY = mouseY - w.requestedBounds.y;
            }
            // Drag-drop: starting a drag from a `canDrag` widget. The
            // drop fires on release over an `acceptsDrop` target with
            // a registered handler.
            if (w.canDrag) {
                this->m_dragSource  = hit;
                this->m_dragPayload = w.dragPayload;
            }
        }

        if (mouseReleased) {
            // Fire click on the widget that was originally pressed, even if
            // the mouse moved slightly between press and release frames
            WidgetId clickTarget = hit;
            if (this->m_pressedWidget != INVALID_WIDGET && this->m_pressedWidget != hit) {
                clickTarget = this->m_pressedWidget;
            }

            Widget* targetWidget = this->getWidget(clickTarget);
            if (targetWidget != nullptr) {
                targetWidget->isPressed = false;
                if (ButtonData* btn = std::get_if<ButtonData>(&targetWidget->data)) {
                    // Disabled buttons swallow the click silently — no
                    // handler, no audio, no event log spam.
                    if (!btn->disabled && btn->onClick) {
                        btn->onClick();
                    }
                    // Audio cue (only if enabled).
                    if (!btn->disabled && btn->clickSound != 0) {
                        this->m_audioOutbox.push_back(btn->clickSound);
                    }
                    // Double-click detection: if previous click on same
                    // widget was within 350 ms, fire onDoubleClick too.
                    auto it = this->m_lastClickTime.find(clickTarget);
                    const float now = this->m_clockSec;
                    if (it != this->m_lastClickTime.end()
                        && (now - it->second) <= 0.35f) {
                        if (btn->onDoubleClick) { btn->onDoubleClick(); }
                        this->logEvent({clickTarget, "dblclick", now});
                        this->m_lastClickTime.erase(it);
                    } else {
                        this->m_lastClickTime[clickTarget] = now;
                    }
                    this->logEvent({clickTarget, "click", now});
                } else if (TabBarData* tabs = std::get_if<TabBarData>(&targetWidget->data)) {
                    // Determine which tab the cursor is over based on
                    // widget-relative X. Tabs stack horizontally with
                    // `tabWidth` each; hit-testing is simple division.
                    const float localX = mouseX - targetWidget->computedBounds.x;
                    if (localX >= 0.0f && tabs->tabWidth > 0.0f) {
                        const int32_t idx = static_cast<int32_t>(localX / tabs->tabWidth);
                        if (idx >= 0 && idx < static_cast<int32_t>(tabs->labels.size())
                            && idx != tabs->activeTab) {
                            tabs->activeTab = idx;
                            if (tabs->onTabSelected) {
                                tabs->onTabSelected(idx);
                            }
                        }
                    }
                } else if (SliderData* slider = std::get_if<SliderData>(&targetWidget->data)) {
                    slider->dragging = false;
                } else if (IconData* icon = std::get_if<IconData>(&targetWidget->data)) {
                    if (icon->onClick) { icon->onClick(); }
                    this->logEvent({clickTarget, "icon", this->m_clockSec});
                } else if (ListRowData* row = std::get_if<ListRowData>(&targetWidget->data)) {
                    // Row fires its own handler and toggles selection
                    // via the multi-select helpers. Ctrl / Shift are
                    // not threaded here yet — callers that need them
                    // can invoke `selectToggle`/`selectRangeTo` from
                    // within onClick.
                    this->selectOnly(clickTarget);
                    if (row->onClick) { row->onClick(); }
                    this->logEvent({clickTarget, "row", this->m_clockSec});
                }
            }
            // Drag-drop: if a drag was in flight and the release lands
            // on a registered drop target, fire its handler.
            if (this->m_dragSource != INVALID_WIDGET) {
                Widget* tgtw = this->getWidget(hit);
                if (tgtw != nullptr && tgtw->acceptsDrop) {
                    auto it = this->m_dropHandlers.find(hit);
                    if (it != this->m_dropHandlers.end() && it->second) {
                        it->second(this->m_dragPayload);
                        this->logEvent({hit, "drop", this->m_clockSec});
                    }
                }
                this->m_dragSource  = INVALID_WIDGET;
                this->m_dragPayload = 0;
            }
            this->m_pressedWidget = INVALID_WIDGET;
            return true;  // Input consumed
        }

        // Right-click path. Mirrors the left-click press/release logic
        // but is opt-in: only widgets with `onRightClick` set react.
        // The primary-click pressed-widget state is intentionally not
        // shared with the right button — right-click is simpler (no
        // drift tolerance) since it's used for context menus.
        if (rightPressed) {
            this->m_rightPressedWidget = hit;
            return true;
        }
        if (rightReleased) {
            WidgetId rightTarget = hit;
            if (this->m_rightPressedWidget != INVALID_WIDGET
                && this->m_rightPressedWidget != hit) {
                rightTarget = this->m_rightPressedWidget;
            }
            Widget* targetWidget = this->getWidget(rightTarget);
            if (targetWidget != nullptr) {
                if (ButtonData* btn = std::get_if<ButtonData>(&targetWidget->data)) {
                    if (btn->onRightClick) {
                        btn->onRightClick();
                    }
                }
            }
            this->m_rightPressedWidget = INVALID_WIDGET;
            return true;
        }

        if (!mousePressed) {
            w.isPressed = false;
        }

        // Only consume input on actual press (not just hover) so that
        // map interactions still work when hovering HUD elements
        if (mousePressed) {
            return true;
        }
        return false;  // Hovering alone doesn't block map clicks
    }

    return false;
}

WidgetId UIManager::hitTest(float x, float y) const {
    // Test root widgets in reverse order (last = topmost)
    for (std::vector<WidgetId>::const_reverse_iterator it = this->m_rootWidgets.rbegin(); it != this->m_rootWidgets.rend(); ++it) {
        WidgetId result = this->hitTestWidget(*it, x, y);
        if (result != INVALID_WIDGET) {
            return result;
        }
    }
    return INVALID_WIDGET;
}

WidgetId UIManager::hitTestWidget(WidgetId id, float x, float y) const {
    const Widget* w = this->getWidget(id);
    if (w == nullptr || !w->isVisible) {
        return INVALID_WIDGET;
    }

    if (!w->computedBounds.contains(x, y)) {
        return INVALID_WIDGET;
    }

    // Test children in reverse order (topmost first)
    for (std::vector<WidgetId>::const_reverse_iterator it = w->children.rbegin(); it != w->children.rend(); ++it) {
        WidgetId result = this->hitTestWidget(*it, x, y);
        if (result != INVALID_WIDGET) {
            return result;
        }
    }

    // Buttons, panels, scroll lists, tab bars, sliders, icons, list
    // rows are clickable; labels and progress bars are passive.
    if (std::holds_alternative<ButtonData>(w->data) ||
        std::holds_alternative<PanelData>(w->data) ||
        std::holds_alternative<ScrollListData>(w->data) ||
        std::holds_alternative<TabBarData>(w->data) ||
        std::holds_alternative<SliderData>(w->data) ||
        std::holds_alternative<IconData>(w->data) ||
        std::holds_alternative<ListRowData>(w->data)) {
        return id;
    }

    return INVALID_WIDGET;
}

// ============================================================================
// Layout
// ============================================================================

void UIManager::shiftWidgetTree(WidgetId id, float deltaX, float deltaY) {
    Widget* w = this->getWidget(id);
    if (w == nullptr) {
        return;
    }
    w->computedBounds.x += deltaX;
    w->computedBounds.y += deltaY;
    for (WidgetId childId : w->children) {
        this->shiftWidgetTree(childId, deltaX, deltaY);
    }
}

void UIManager::setScreenSize(float width, float height) {
    this->m_screenWidth  = width;
    this->m_screenHeight = height;
}

void UIManager::layout() {
    for (WidgetId rootId : this->m_rootWidgets) {
        this->layoutWidget(rootId, 0.0f, 0.0f);
    }

    // Apply anchor adjustments to root widgets after standard layout.
    // Only root widgets (no parent) use anchors; children are relative to parent.
    for (WidgetId rootId : this->m_rootWidgets) {
        Widget* w = this->getWidget(rootId);
        if (w == nullptr || !w->isVisible || w->anchor == Anchor::None) {
            continue;
        }

        const float widgetW = w->computedBounds.w;
        const float widgetH = w->computedBounds.h;
        float newX = w->computedBounds.x;
        float newY = w->computedBounds.y;

        switch (w->anchor) {
            case Anchor::None:
                break;
            case Anchor::TopLeft:
                // requestedBounds.x/y are the margins from top-left (default behavior)
                newX = w->requestedBounds.x;
                newY = w->requestedBounds.y;
                break;
            case Anchor::TopRight:
                newX = this->m_screenWidth - w->marginRight - widgetW;
                newY = w->requestedBounds.y;
                break;
            case Anchor::BottomLeft:
                newX = w->requestedBounds.x;
                newY = this->m_screenHeight - w->marginBottom - widgetH;
                break;
            case Anchor::BottomRight:
                newX = this->m_screenWidth - w->marginRight - widgetW;
                newY = this->m_screenHeight - w->marginBottom - widgetH;
                break;
            case Anchor::Center:
                newX = (this->m_screenWidth - widgetW) * 0.5f;
                newY = (this->m_screenHeight - widgetH) * 0.5f;
                break;
            case Anchor::TopCenter:
                newX = (this->m_screenWidth - widgetW) * 0.5f;
                newY = w->requestedBounds.y;
                break;
            case Anchor::BottomCenter:
                newX = (this->m_screenWidth - widgetW) * 0.5f;
                newY = this->m_screenHeight - w->marginBottom - widgetH;
                break;
        }

        // Shift the entire subtree by the delta between old and new position
        float deltaX = newX - w->computedBounds.x;
        float deltaY = newY - w->computedBounds.y;
        if (deltaX != 0.0f || deltaY != 0.0f) {
            this->shiftWidgetTree(rootId, deltaX, deltaY);
        }
    }
}

void UIManager::layoutWidget(WidgetId id, float parentX, float parentY) {
    Widget* w = this->getWidget(id);
    if (w == nullptr || !w->isVisible) {
        return;
    }

    // Compute absolute position from parent + requested offset
    w->computedBounds.x = parentX + w->requestedBounds.x;
    w->computedBounds.y = parentY + w->requestedBounds.y;
    w->computedBounds.w = w->requestedBounds.w;
    w->computedBounds.h = w->requestedBounds.h;

    // For ScrollList parents, apply scroll offset to children
    float scrollOffsetY = 0.0f;
    if (ScrollListData* scrollData = std::get_if<ScrollListData>(&w->data)) {
        scrollOffsetY = -scrollData->scrollOffset;
    }

    // Layout children (stacking with padding, spacing, flex, grid)
    const float contentX = w->computedBounds.x + w->padding.left;
    const float contentY = w->computedBounds.y + w->padding.top + scrollOffsetY;
    const float contentW = w->computedBounds.w - w->padding.left - w->padding.right;
    const float contentH = w->computedBounds.h - w->padding.top - w->padding.bottom;

    // Grid mode: uniform cells in a row-major layout. Grid takes
    // priority over flex because the two use orthogonal geometry.
    if (w->gridColumns > 0) {
        const int32_t cols = w->gridColumns;
        std::vector<WidgetId> visibleChildren;
        visibleChildren.reserve(w->children.size());
        for (WidgetId c : w->children) {
            Widget* cw = this->getWidget(c);
            if (cw != nullptr && cw->isVisible) { visibleChildren.push_back(c); }
        }
        const int32_t rows = (static_cast<int32_t>(visibleChildren.size()) + cols - 1) / cols;
        const float cellW = (contentW - static_cast<float>(cols - 1) * w->childSpacing)
                            / static_cast<float>(cols);
        const float cellH = (rows > 0)
            ? (contentH - static_cast<float>(rows - 1) * w->childSpacing)
                  / static_cast<float>(rows)
            : 0.0f;
        for (std::size_t i = 0; i < visibleChildren.size(); ++i) {
            Widget* child = this->getWidget(visibleChildren[i]);
            if (child == nullptr) { continue; }
            child->requestedBounds.w = cellW;
            child->requestedBounds.h = cellH;
            const int32_t col = static_cast<int32_t>(i) % cols;
            const int32_t row = static_cast<int32_t>(i) / cols;
            const float cx = contentX + static_cast<float>(col) * (cellW + w->childSpacing);
            const float cy = contentY + static_cast<float>(row) * (cellH + w->childSpacing);
            this->layoutWidget(visibleChildren[i], cx, cy);
        }
        return;
    }

    // Flex pass: pre-compute the leftover space after intrinsic-sized
    // siblings take their share, then allocate it proportionally to any
    // `child->flex > 0` children along the parent's layout axis.
    float flexSum = 0.0f;
    float intrinsicTotal = 0.0f;
    int32_t visibleCount = 0;
    for (WidgetId childId : w->children) {
        Widget* child = this->getWidget(childId);
        if (child == nullptr || !child->isVisible) { continue; }
        ++visibleCount;
        if (child->flex > 0.0f) {
            flexSum += child->flex;
        } else if (w->layoutDirection == LayoutDirection::Vertical) {
            intrinsicTotal += child->requestedBounds.h;
        } else {
            intrinsicTotal += child->requestedBounds.w;
        }
    }
    const float axisTotal = (w->layoutDirection == LayoutDirection::Vertical)
        ? contentH : contentW;
    const float spacingTotal = (visibleCount > 1)
        ? static_cast<float>(visibleCount - 1) * w->childSpacing
        : 0.0f;
    const float flexPool = (flexSum > 0.0f)
        ? std::max(0.0f, axisTotal - intrinsicTotal - spacingTotal)
        : 0.0f;

    float cursorX = contentX;
    float cursorY = contentY;
    float currentRowHeight = 0.0f;  // Used by HorizontalWrap only.

    for (WidgetId childId : w->children) {
        Widget* child = this->getWidget(childId);
        if (child == nullptr || !child->isVisible) {
            continue;
        }

        // Flex children grab their share of the pool before layout.
        if (child->flex > 0.0f && flexSum > 0.0f) {
            const float share = flexPool * (child->flex / flexSum);
            if (w->layoutDirection == LayoutDirection::Vertical) {
                child->requestedBounds.h = share;
            } else if (w->layoutDirection == LayoutDirection::Horizontal) {
                child->requestedBounds.w = share;
            }
        }

        // Cross-axis fill: in a Vertical container a child's width
        // defaults to the parent's content width when the child opts
        // in (via `fillParentCross` or `w == 0`). Horizontal mirrors
        // with height. Kills the "set w=230 to match parent" ritual
        // that litters screen code.
        if (w->layoutDirection == LayoutDirection::Vertical) {
            if (child->fillParentCross || child->requestedBounds.w <= 0.0f) {
                child->requestedBounds.w = contentW;
            }
        } else if (w->layoutDirection == LayoutDirection::Horizontal) {
            if (child->fillParentCross || child->requestedBounds.h <= 0.0f) {
                child->requestedBounds.h = contentH;
            }
        }

        // Cheap defense: a child whose intrinsic width exceeds the
        // parent's content area would overflow. When `clampChildren`
        // is on (the default), shrink the requested size before
        // layout so the child draws inside the panel. ScrollList
        // parents keep the height axis un-clamped — scrolling
        // deliberately overflows the visible window on Y — but
        // horizontal clamping still applies so bars don't spill
        // sideways.
        const bool isScrollListParent =
            std::holds_alternative<ScrollListData>(w->data);
        if (w->clampChildren) {
            if (child->requestedBounds.w > contentW) {
                if (this->m_strictLayout) {
                    LOG_WARN("[strict-layout] widget %u w=%.0f exceeds parent contentW=%.0f",
                             static_cast<unsigned>(child->id),
                             static_cast<double>(child->requestedBounds.w),
                             static_cast<double>(contentW));
                }
                child->requestedBounds.w = contentW;
            }
            if (!isScrollListParent && child->requestedBounds.h > contentH) {
                if (this->m_strictLayout) {
                    LOG_WARN("[strict-layout] widget %u h=%.0f exceeds parent contentH=%.0f",
                             static_cast<unsigned>(child->id),
                             static_cast<double>(child->requestedBounds.h),
                             static_cast<double>(contentH));
                }
                child->requestedBounds.h = contentH;
            }
        }

        // Per-widget min/max constraints. Run after clamp so caller's
        // minimum size can pull a too-shrunk flex child back up, and
        // maximum size caps an over-filled one.
        if (child->minW > 0.0f && child->requestedBounds.w < child->minW) {
            child->requestedBounds.w = child->minW;
        }
        if (child->maxW > 0.0f && child->requestedBounds.w > child->maxW) {
            child->requestedBounds.w = child->maxW;
        }
        if (child->minH > 0.0f && child->requestedBounds.h < child->minH) {
            child->requestedBounds.h = child->minH;
        }
        if (child->maxH > 0.0f && child->requestedBounds.h > child->maxH) {
            child->requestedBounds.h = child->maxH;
        }

        if (w->layoutDirection == LayoutDirection::Vertical) {
            this->layoutWidget(childId, cursorX, cursorY);
            // Post-layout clamp: computed bounds may still exceed
            // parent because child has internal padding / spacing.
            // Horizontal clamp applies even to ScrollList parents so
            // widgets can't render past the right edge.
            if (w->clampChildren) {
                const float overflow = (child->computedBounds.x + child->computedBounds.w)
                                       - (contentX + contentW);
                if (overflow > 0.0f) {
                    child->computedBounds.w -= overflow;
                }
            }
            cursorY += child->computedBounds.h + w->childSpacing;
        } else if (w->layoutDirection == LayoutDirection::HorizontalWrap) {
            // Wrap to a new row when this child would overflow contentW.
            const float childW = child->requestedBounds.w;
            if (cursorX > contentX
                && (cursorX - contentX) + childW > contentW) {
                cursorX = contentX;
                cursorY += currentRowHeight + w->childSpacing;
                currentRowHeight = 0.0f;
            }
            this->layoutWidget(childId, cursorX, cursorY);
            cursorX += child->computedBounds.w + w->childSpacing;
            if (child->computedBounds.h > currentRowHeight) {
                currentRowHeight = child->computedBounds.h;
            }
        } else {
            this->layoutWidget(childId, cursorX, cursorY);
            cursorX += child->computedBounds.w + w->childSpacing;
        }
    }

    // Update contentHeight for ScrollList widgets
    if (ScrollListData* scrollData = std::get_if<ScrollListData>(&w->data)) {
        float totalHeight = (cursorY - scrollOffsetY) -
                            (w->computedBounds.y + w->padding.top);
        if (!w->children.empty()) {
            totalHeight -= w->childSpacing;  // Remove trailing spacing
        }
        scrollData->contentHeight = totalHeight;
    }
}

// ============================================================================
// Rendering
// ============================================================================

void UIManager::render(vulkan_app::renderer::Renderer2D& renderer2d) const {
    for (WidgetId rootId : this->m_rootWidgets) {
        this->renderWidget(renderer2d, rootId);
    }
}

void UIManager::renderWidget(vulkan_app::renderer::Renderer2D& renderer2d,
                              WidgetId id) const {
    const Widget* w = this->getWidget(id);
    if (w == nullptr || !w->isVisible) {
        return;
    }

    const Rect& b = w->computedBounds;

    const float scale = this->m_renderScale;

    // auto required: generic lambda template parameter
    std::visit([&](const auto& data) {
        using T = std::decay_t<decltype(data)>;

        if constexpr (std::is_same_v<T, PanelData>) {
            // Base fill — `Widget.alpha` modulates so fade tweens
            // ripple through. Gradient bottom is layered on top as a
            // second band if provided.
            const float a = data.backgroundColor.a * w->alpha;
            float cr = data.cornerRadius * scale;
            if (cr > 0.0f) {
                renderer2d.drawRoundedRect(b.x, b.y, b.w, b.h, cr,
                                            data.backgroundColor.r, data.backgroundColor.g,
                                            data.backgroundColor.b, a);
            } else {
                renderer2d.drawFilledRect(b.x, b.y, b.w, b.h,
                                           data.backgroundColor.r, data.backgroundColor.g,
                                           data.backgroundColor.b, a);
            }
            // Two-band gradient. Cheap — no shader change — just
            // blends the bottom half toward `gradientBottom`. Six
            // slices give a visible gradient without the banding an
            // extreme step count introduces.
            if (data.gradientBottom.a > 0.0f) {
                constexpr int32_t kSlices = 6;
                const float sliceH = b.h / static_cast<float>(kSlices);
                for (int32_t s = 0; s < kSlices; ++s) {
                    const float t = static_cast<float>(s)
                                    / static_cast<float>(kSlices - 1);
                    const float rr = data.backgroundColor.r * (1.0f - t)
                                   + data.gradientBottom.r * t;
                    const float gg = data.backgroundColor.g * (1.0f - t)
                                   + data.gradientBottom.g * t;
                    const float bb = data.backgroundColor.b * (1.0f - t)
                                   + data.gradientBottom.b * t;
                    const float aa = (data.backgroundColor.a * (1.0f - t)
                                    + data.gradientBottom.a * t) * w->alpha;
                    renderer2d.drawFilledRect(
                        b.x, b.y + static_cast<float>(s) * sliceH,
                        b.w, sliceH, rr, gg, bb, aa);
                }
            }
            // Border.
            if (data.borderColor.a > 0.0f) {
                const float bw = std::max(1.0f, data.borderWidth * scale);
                const float ba = data.borderColor.a * w->alpha;
                renderer2d.drawFilledRect(b.x, b.y, b.w, bw,
                                           data.borderColor.r, data.borderColor.g,
                                           data.borderColor.b, ba);
                renderer2d.drawFilledRect(b.x, b.y + b.h - bw, b.w, bw,
                                           data.borderColor.r, data.borderColor.g,
                                           data.borderColor.b, ba);
                renderer2d.drawFilledRect(b.x, b.y, bw, b.h,
                                           data.borderColor.r, data.borderColor.g,
                                           data.borderColor.b, ba);
                renderer2d.drawFilledRect(b.x + b.w - bw, b.y, bw, b.h,
                                           data.borderColor.r, data.borderColor.g,
                                           data.borderColor.b, ba);
            }
            // Top highlight + bottom shadow: cheap depth cues.
            if (data.topHighlight.a > 0.0f) {
                renderer2d.drawFilledRect(b.x, b.y, b.w, 1.0f * scale,
                    data.topHighlight.r, data.topHighlight.g,
                    data.topHighlight.b, data.topHighlight.a * w->alpha);
            }
            if (data.bottomShadow.a > 0.0f) {
                renderer2d.drawFilledRect(b.x, b.y + b.h - 1.0f * scale, b.w, 1.0f * scale,
                    data.bottomShadow.r, data.bottomShadow.g,
                    data.bottomShadow.b, data.bottomShadow.a * w->alpha);
            }
            // Leading accent bar — Civ-6 style ribbon.
            if (data.accentBarColor.a > 0.0f) {
                const float abw = data.accentBarWidth * scale;
                renderer2d.drawFilledRect(b.x, b.y, abw, b.h,
                    data.accentBarColor.r, data.accentBarColor.g,
                    data.accentBarColor.b, data.accentBarColor.a * w->alpha);
            }
        }
        else if constexpr (std::is_same_v<T, ButtonData>) {
            // Priority: disabled > pressed > selected > hover > normal.
            // `selected` persists after mouseup so tabs/research picks
            // show the active state even when the cursor leaves.
            Color color = data.normalColor;
            if (data.disabled) {
                color = {0.18f, 0.18f, 0.22f, 0.6f};
            } else if (w->isPressed) {
                color = data.pressedColor;
            } else if (data.selected) {
                color = data.selectedColor;
            } else if (w->isHovered) {
                color = data.hoverColor;
            }

            float cr = data.cornerRadius * scale;
            if (cr > 0.0f) {
                renderer2d.drawRoundedRect(b.x, b.y, b.w, b.h, cr,
                                            color.r, color.g, color.b, color.a);
            } else {
                renderer2d.drawFilledRect(b.x, b.y, b.w, b.h,
                                           color.r, color.g, color.b, color.a);
            }

            // Gradient bottom band for glossy depth. Six horizontal
            // slices blend toward `gradientBottom` mixed with the
            // active state colour. Off when alpha 0.
            if (data.gradientBottom.a > 0.0f) {
                constexpr int32_t kSlices = 5;
                const float sliceH = b.h / static_cast<float>(kSlices);
                for (int32_t s = 1; s < kSlices; ++s) {
                    const float t = static_cast<float>(s)
                                    / static_cast<float>(kSlices - 1);
                    const float rr = color.r * (1.0f - t)
                                   + data.gradientBottom.r * t;
                    const float gg = color.g * (1.0f - t)
                                   + data.gradientBottom.g * t;
                    const float bb = color.b * (1.0f - t)
                                   + data.gradientBottom.b * t;
                    const float aa = (color.a * (1.0f - t)
                                    + data.gradientBottom.a * t);
                    renderer2d.drawFilledRect(
                        b.x, b.y + static_cast<float>(s) * sliceH,
                        b.w, sliceH, rr, gg, bb, aa);
                }
            }

            // Thin outline for readability on busy backgrounds.
            if (data.borderColor.a > 0.0f) {
                const float bw = std::max(1.0f, data.borderWidth * scale);
                renderer2d.drawFilledRect(b.x, b.y, b.w, bw,
                    data.borderColor.r, data.borderColor.g,
                    data.borderColor.b, data.borderColor.a);
                renderer2d.drawFilledRect(b.x, b.y + b.h - bw, b.w, bw,
                    data.borderColor.r, data.borderColor.g,
                    data.borderColor.b, data.borderColor.a);
                renderer2d.drawFilledRect(b.x, b.y, bw, b.h,
                    data.borderColor.r, data.borderColor.g,
                    data.borderColor.b, data.borderColor.a);
                renderer2d.drawFilledRect(b.x + b.w - bw, b.y, bw, b.h,
                    data.borderColor.r, data.borderColor.g,
                    data.borderColor.b, data.borderColor.a);
            }

            // Depth effect: raised highlight on top edge when idle,
            // inset shadow when pressed. Cheap 1px lines, readable
            // affordance without needing a full 3D look.
            if (!data.disabled) {
                const float edge = 1.0f * scale;
                if (w->isPressed) {
                    // inset: darken top + left
                    renderer2d.drawFilledRect(b.x, b.y, b.w, edge,
                                               0.0f, 0.0f, 0.0f, 0.35f);
                    renderer2d.drawFilledRect(b.x, b.y, edge, b.h,
                                               0.0f, 0.0f, 0.0f, 0.35f);
                } else {
                    // raised: brighten top
                    renderer2d.drawFilledRect(b.x, b.y, b.w, edge,
                                               1.0f, 1.0f, 1.0f, 0.15f);
                }
            }

            // Focus ring: thin border when widget has keyboard focus.
            if (w->isFocused) {
                const float ring = 1.5f * scale;
                renderer2d.drawFilledRect(b.x, b.y, b.w, ring,
                                           1.0f, 0.9f, 0.3f, 0.9f);
                renderer2d.drawFilledRect(b.x, b.y + b.h - ring, b.w, ring,
                                           1.0f, 0.9f, 0.3f, 0.9f);
                renderer2d.drawFilledRect(b.x, b.y, ring, b.h,
                                           1.0f, 0.9f, 0.3f, 0.9f);
                renderer2d.drawFilledRect(b.x + b.w - ring, b.y, ring, b.h,
                                           1.0f, 0.9f, 0.3f, 0.9f);
            }

            // Optional leading icon — from IconAtlas by spriteId.
            // Rendered at left edge; label offset accounts for it.
            float labelOffset = 0.0f;
            if (data.iconSpriteId != 0) {
                Color ic{0.6f, 0.6f, 0.6f, 1.0f};
                const IconRegion* reg =
                    IconAtlas::instance().region(data.iconSpriteId);
                if (reg != nullptr) { ic = reg->fallback; }
                const float iz = data.iconSize * scale;
                renderer2d.drawFilledRect(b.x + 4.0f * scale,
                                           b.y + (b.h - iz) * 0.5f,
                                           iz, iz, ic.r, ic.g, ic.b, ic.a);
                labelOffset = iz + 6.0f * scale;
            }
            // Center the label text in the remaining area. Pass pixelScale so each rasterized pixel
            // is scale x scale world units, which the shader zooms back to 1x1 screen pixels.
            if (!data.label.empty()) {
                float worldFontSize = data.fontSize * scale;
                Rect textBounds = BitmapFont::measureText(data.label, data.fontSize);
                float textW = textBounds.w * scale;
                float textH = textBounds.h * scale;
                float textX = b.x + labelOffset
                            + (b.w - labelOffset - textW) * 0.5f;
                float textY = b.y + (b.h - textH) * 0.5f;
                BitmapFont::drawText(renderer2d, data.label, textX, textY,
                                      worldFontSize, data.labelColor, scale);
            }
        }
        else if constexpr (std::is_same_v<T, LabelData>) {
            if (!data.text.empty()) {
                float worldFontSize = data.fontSize * scale;
                BitmapFont::drawText(renderer2d, data.text, b.x, b.y,
                                      worldFontSize, data.color, scale);
            }
        }
        else if constexpr (std::is_same_v<T, ScrollListData>) {
            renderer2d.drawFilledRect(b.x, b.y, b.w, b.h,
                                       data.backgroundColor.r, data.backgroundColor.g,
                                       data.backgroundColor.b, data.backgroundColor.a);
        }
        else if constexpr (std::is_same_v<T, TabBarData>) {
            // One coloured pill per tab, horizontally stacked. An
            // underline bar slides between tabs using `activeTabAnim`
            // so selection transitions visibly animate.
            const float tabW = data.tabWidth * scale;
            for (std::size_t i = 0; i < data.labels.size(); ++i) {
                const float tx = b.x + static_cast<float>(i) * tabW;
                const bool isActive = static_cast<int32_t>(i) == data.activeTab;
                Color c = isActive ? data.activeColor : data.inactiveColor;
                if (w->isHovered && !isActive) { c = data.hoverColor; }
                renderer2d.drawFilledRect(tx, b.y, tabW, b.h,
                                           c.r, c.g, c.b, c.a);
                const float worldFontSize = data.fontSize * scale;
                Rect tb = BitmapFont::measureText(data.labels[i], data.fontSize);
                const float textW = tb.w * scale;
                const float textH = tb.h * scale;
                const float textX = tx + (tabW - textW) * 0.5f;
                const float textY = b.y + (b.h - textH) * 0.5f;
                BitmapFont::drawText(renderer2d, data.labels[i], textX, textY,
                                      worldFontSize, data.labelColor, scale);
            }
            // Animated underline bar.
            const float ux = b.x + data.activeTabAnim * tabW;
            const float uThick = data.underlineThickness * scale;
            renderer2d.drawFilledRect(ux, b.y + b.h - uThick, tabW, uThick,
                                       data.activeColor.r, data.activeColor.g,
                                       data.activeColor.b, 1.0f);
        }
        else if constexpr (std::is_same_v<T, ProgressBarData>) {
            const float cr = data.cornerRadius * scale;
            if (cr > 0.0f) {
                renderer2d.drawRoundedRect(b.x, b.y, b.w, b.h, cr,
                                            data.backgroundColor.r, data.backgroundColor.g,
                                            data.backgroundColor.b, data.backgroundColor.a);
            } else {
                renderer2d.drawFilledRect(b.x, b.y, b.w, b.h,
                                           data.backgroundColor.r, data.backgroundColor.g,
                                           data.backgroundColor.b, data.backgroundColor.a);
            }
            float frac = data.fillFraction;
            if (frac < 0.0f) { frac = 0.0f; }
            if (frac > 1.0f) { frac = 1.0f; }
            const float fillW = b.w * frac;
            if (fillW > 0.0f) {
                if (cr > 0.0f) {
                    renderer2d.drawRoundedRect(b.x, b.y, fillW, b.h, cr,
                                                data.fillColor.r, data.fillColor.g,
                                                data.fillColor.b, data.fillColor.a);
                } else {
                    renderer2d.drawFilledRect(b.x, b.y, fillW, b.h,
                                               data.fillColor.r, data.fillColor.g,
                                               data.fillColor.b, data.fillColor.a);
                }
            }
            if (!data.overlayText.empty()) {
                const float worldFontSize = data.fontSize * scale;
                Rect tb = BitmapFont::measureText(data.overlayText, data.fontSize);
                const float textW = tb.w * scale;
                const float textH = tb.h * scale;
                BitmapFont::drawText(renderer2d, data.overlayText,
                                      b.x + (b.w - textW) * 0.5f,
                                      b.y + (b.h - textH) * 0.5f,
                                      worldFontSize, data.textColor, scale);
            }
        }
        else if constexpr (std::is_same_v<T, SliderData>) {
            // Track.
            renderer2d.drawFilledRect(b.x, b.y + b.h * 0.4f, b.w, b.h * 0.2f,
                                       data.trackColor.r, data.trackColor.g,
                                       data.trackColor.b, data.trackColor.a);
            // Fill up to the current value.
            float t = 0.0f;
            if (data.maxValue > data.minValue) {
                t = (data.value - data.minValue) / (data.maxValue - data.minValue);
            }
            if (t < 0.0f) { t = 0.0f; }
            if (t > 1.0f) { t = 1.0f; }
            renderer2d.drawFilledRect(b.x, b.y + b.h * 0.4f, b.w * t, b.h * 0.2f,
                                       data.fillColor.r, data.fillColor.g,
                                       data.fillColor.b, data.fillColor.a);
            // Thumb.
            const float thumbW = 8.0f * scale;
            const float thumbX = b.x + b.w * t - thumbW * 0.5f;
            renderer2d.drawFilledRect(thumbX, b.y, thumbW, b.h,
                                       data.thumbColor.r, data.thumbColor.g,
                                       data.thumbColor.b, data.thumbColor.a);
        }
        else if constexpr (std::is_same_v<T, IconData>) {
            // Real sprite path pending — pull the placeholder colour
            // from the IconAtlas when the widget carries a registered
            // spriteId. Unknown ids fall back to the widget's own
            // `fallbackColor` so ad-hoc icons still draw something.
            Color c = data.fallbackColor;
            if (data.spriteId != 0) {
                const IconRegion* reg = IconAtlas::instance().region(data.spriteId);
                if (reg != nullptr) { c = reg->fallback; }
            }
            renderer2d.drawFilledRect(b.x, b.y, b.w, b.h,
                                       c.r * data.tint.r, c.g * data.tint.g,
                                       c.b * data.tint.b, data.tint.a);
        }
        else if constexpr (std::is_same_v<T, RichTextData>) {
            // Walk spans left-to-right, advancing cursor by measured
            // span width. Icons render as tinted boxes pending the
            // sprite pipeline. Wrapping wraps on whitespace boundaries
            // when `wrapWidth > 0`.
            float cursorX = b.x;
            float cursorY = b.y;
            const float worldFontSize = data.fontSize * scale;
            const float lineHeight = (data.fontSize + 2.0f) * scale;
            for (const RichTextSpan& span : data.spans) {
                if (span.kind == RichTextSpan::Kind::LineBreak) {
                    cursorX = b.x;
                    cursorY += lineHeight;
                    continue;
                }
                if (span.kind == RichTextSpan::Kind::Icon) {
                    const float iw = data.fontSize * scale;
                    renderer2d.drawFilledRect(cursorX, cursorY, iw, iw,
                                               span.color.r, span.color.g,
                                               span.color.b, span.color.a);
                    cursorX += iw + scale * 2.0f;
                    continue;
                }
                if (!span.text.empty()) {
                    Rect tb = BitmapFont::measureText(span.text, data.fontSize);
                    const float tw = tb.w * scale;
                    if (data.wrapWidth > 0.0f
                        && (cursorX - b.x) + tw > data.wrapWidth) {
                        cursorX = b.x;
                        cursorY += lineHeight;
                    }
                    BitmapFont::drawText(renderer2d, span.text, cursorX, cursorY,
                                          worldFontSize, span.color, scale);
                    cursorX += tw + scale * 2.0f;
                }
            }
        }
        else if constexpr (std::is_same_v<T, PortraitData>) {
            // Background sprite fallback.
            Color c = data.fallbackColor;
            renderer2d.drawFilledRect(b.x, b.y, b.w, b.h * 0.6f,
                                       c.r * data.tint.r, c.g * data.tint.g,
                                       c.b * data.tint.b, data.tint.a);
            // Title strip.
            const float worldTitle = data.titleFontSize * scale;
            BitmapFont::drawText(renderer2d, data.title,
                                  b.x + 6.0f * scale,
                                  b.y + b.h * 0.62f,
                                  worldTitle, data.titleColor, scale);
            // Stats grid: two columns of key:value lines.
            const float worldStat = data.statsFontSize * scale;
            const float lineH = (data.statsFontSize + 2.0f) * scale;
            float sy = b.y + b.h * 0.78f;
            for (const std::pair<std::string, std::string>& kv : data.stats) {
                BitmapFont::drawText(renderer2d, kv.first,
                                      b.x + 6.0f * scale, sy,
                                      worldStat,
                                      Color{0.7f, 0.7f, 0.7f, 1.0f}, scale);
                BitmapFont::drawText(renderer2d, kv.second,
                                      b.x + b.w * 0.55f, sy,
                                      worldStat, data.titleColor, scale);
                sy += lineH;
            }
        }
        else if constexpr (std::is_same_v<T, ListRowData>) {
            // Row background: hover / pressed / selected variants
            // layered with the optional left accent bar.
            Color bg = {0.0f, 0.0f, 0.0f, 0.0f};
            if (w->isPressed)      { bg = data.pressedBg; }
            else if (w->isHovered) { bg = data.hoverBg; }
            if (bg.a > 0.0f) {
                renderer2d.drawFilledRect(b.x, b.y, b.w, b.h,
                                           bg.r, bg.g, bg.b, bg.a);
            }
            if (w->isSelected) {
                // Accent edge on the left, 4px wide.
                const float accW = 4.0f * scale;
                renderer2d.drawFilledRect(b.x, b.y, accW, b.h,
                                           data.accentColor.r, data.accentColor.g,
                                           data.accentColor.b, data.accentColor.a);
            }

            float cursorX = b.x + 8.0f * scale;
            // Leading icon.
            if (data.iconSpriteId != 0) {
                Color ic{0.5f, 0.5f, 0.5f, 1.0f};
                const IconRegion* reg = IconAtlas::instance().region(data.iconSpriteId);
                if (reg != nullptr) { ic = reg->fallback; }
                const float iz = data.iconSize * scale;
                renderer2d.drawFilledRect(cursorX, b.y + (b.h - iz) * 0.5f, iz, iz,
                                           ic.r, ic.g, ic.b, ic.a);
                cursorX += iz + 8.0f * scale;
            }

            // Title + subtitle stack.
            const float titleFont = data.titleFont * scale;
            const float subFont   = data.subtitleFont * scale;
            if (!data.subtitle.empty()) {
                BitmapFont::drawText(renderer2d, data.title,
                                      cursorX, b.y + 4.0f * scale,
                                      titleFont, data.titleColor, scale);
                BitmapFont::drawText(renderer2d, data.subtitle,
                                      cursorX, b.y + b.h * 0.55f,
                                      subFont, data.subtitleColor, scale);
            } else {
                BitmapFont::drawText(renderer2d, data.title,
                                      cursorX, b.y + (b.h - titleFont) * 0.5f,
                                      titleFont, data.titleColor, scale);
            }

            // Right-aligned value. Measured to position inside bounds.
            if (!data.rightValue.empty()) {
                const float vFont = data.valueFont * scale;
                Rect mb = BitmapFont::measureText(data.rightValue, data.valueFont);
                const float vw = mb.w * scale;
                BitmapFont::drawText(renderer2d, data.rightValue,
                                      b.x + b.w - vw - 8.0f * scale,
                                      b.y + (b.h - vFont) * 0.5f,
                                      vFont, data.valueColor, scale);
            }
        }
        else if constexpr (std::is_same_v<T, MarkdownData>) {
            // Minimal renderer: split source on '\n', dispatch by line
            // prefix. Headings render in heading colour at +4 font.
            const float baseFont = data.fontSize * scale;
            const float headingFont = (data.fontSize + 4.0f) * scale;
            const float lineH = (data.fontSize + 4.0f) * scale;
            float cursorY = b.y;
            std::size_t start = 0;
            const std::string& src = data.source;
            for (std::size_t i = 0; i <= src.size(); ++i) {
                if (i == src.size() || src[i] == '\n') {
                    std::string line = src.substr(start, i - start);
                    Color color = data.textColor;
                    float font  = baseFont;
                    if (line.rfind("## ", 0) == 0) {
                        line = line.substr(3);
                        color = data.headingColor;
                        font  = headingFont;
                    } else if (line.rfind("# ", 0) == 0) {
                        line = line.substr(2);
                        color = data.headingColor;
                        font  = headingFont;
                    } else if (line.rfind("- ", 0) == 0) {
                        line = "  • " + line.substr(2);
                    }
                    if (!line.empty()) {
                        BitmapFont::drawText(renderer2d, line, b.x, cursorY,
                                              font, color, scale);
                    }
                    cursorY += lineH;
                    start = i + 1;
                }
            }
        }
    }, w->data);

    // Render children -- for ScrollList, clip to visible window
    const bool isScrollList = std::holds_alternative<ScrollListData>(w->data);
    // Push a Vulkan scissor before descending into children when
    // `clipChildren` is set and the caller provided a command buffer.
    // Guarantees geometry outside the panel never hits the swapchain.
    const bool pushedScissor = w->clipChildren && this->m_cmdBuffer != nullptr;
    if (pushedScissor) {
        renderer2d.pushScissor(b.x, b.y, b.w, b.h,
                                static_cast<VkCommandBuffer>(this->m_cmdBuffer));
    }
    for (WidgetId childId : w->children) {
        if (isScrollList) {
            const Widget* child = this->getWidget(childId);
            if (child == nullptr || !child->isVisible) {
                continue;
            }
            const Rect& cb = child->computedBounds;
            // Skip children entirely outside the visible window
            if (cb.y + cb.h < b.y || cb.y > b.y + b.h) {
                continue;
            }
        }
        this->renderWidget(renderer2d, childId);
    }
    if (pushedScissor) {
        renderer2d.popScissor(static_cast<VkCommandBuffer>(this->m_cmdBuffer));
    }
}

void UIManager::transformBounds(float cameraX, float cameraY, float invZoom) {
    this->m_renderScale = invZoom;
    for (Widget& w : this->m_widgets) {
        if (w.id == INVALID_WIDGET) {
            continue;
        }
        w.computedBounds.x = cameraX + w.computedBounds.x * invZoom;
        w.computedBounds.y = cameraY + w.computedBounds.y * invZoom;
        w.computedBounds.w = w.computedBounds.w * invZoom;
        w.computedBounds.h = w.computedBounds.h * invZoom;
    }
}

void UIManager::untransformBounds(float cameraX, float cameraY, float invZoom) {
    this->m_renderScale = 1.0f;
    float zoom = 1.0f / invZoom;
    for (Widget& w : this->m_widgets) {
        if (w.id == INVALID_WIDGET) {
            continue;
        }
        w.computedBounds.x = (w.computedBounds.x - cameraX) * zoom;
        w.computedBounds.y = (w.computedBounds.y - cameraY) * zoom;
        w.computedBounds.w = w.computedBounds.w * zoom;
        w.computedBounds.h = w.computedBounds.h * zoom;
    }
}

} // namespace aoc::ui
