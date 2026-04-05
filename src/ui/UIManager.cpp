/**
 * @file UIManager.cpp
 * @brief Widget tree management, layout, input, and rendering.
 */

#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/BitmapFont.hpp"

#include <renderer/Renderer2D.hpp>

#include <algorithm>
#include <cassert>

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

    w.id = INVALID_WIDGET;
    this->m_freeList.push_back(id);
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

// ============================================================================
// Input
// ============================================================================

bool UIManager::handleInput(float mouseX, float mouseY,
                             bool mousePressed, bool mouseReleased,
                             float scrollDelta) {
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

        // Scroll wheel: walk up from hovered widget to find a ScrollList ancestor
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
        }

        if (mousePressed) {
            w.isPressed = true;
            this->m_pressedWidget = hit;
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
                    if (btn->onClick) {
                        btn->onClick();
                    }
                }
            }
            this->m_pressedWidget = INVALID_WIDGET;
            return true;  // Input consumed
        }

        if (!mousePressed) {
            w.isPressed = false;
        }

        return true;  // Hovering over UI consumes hover
    }

    return false;
}

WidgetId UIManager::hitTest(float x, float y) const {
    // Test root widgets in reverse order (last = topmost)
    for (auto it = this->m_rootWidgets.rbegin(); it != this->m_rootWidgets.rend(); ++it) {
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
    for (auto it = w->children.rbegin(); it != w->children.rend(); ++it) {
        WidgetId result = this->hitTestWidget(*it, x, y);
        if (result != INVALID_WIDGET) {
            return result;
        }
    }

    // Buttons, panels, and scroll lists are clickable; labels are not
    if (std::holds_alternative<ButtonData>(w->data) ||
        std::holds_alternative<PanelData>(w->data) ||
        std::holds_alternative<ScrollListData>(w->data)) {
        return id;
    }

    return INVALID_WIDGET;
}

// ============================================================================
// Layout
// ============================================================================

void UIManager::layout() {
    for (WidgetId rootId : this->m_rootWidgets) {
        this->layoutWidget(rootId, 0.0f, 0.0f);
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

    // Layout children (simple stacking with padding and spacing)
    float contentX = w->computedBounds.x + w->padding.left;
    float contentY = w->computedBounds.y + w->padding.top + scrollOffsetY;
    float cursorX = contentX;
    float cursorY = contentY;

    for (WidgetId childId : w->children) {
        Widget* child = this->getWidget(childId);
        if (child == nullptr || !child->isVisible) {
            continue;
        }

        if (w->layoutDirection == LayoutDirection::Vertical) {
            this->layoutWidget(childId, cursorX, cursorY);
            cursorY += child->computedBounds.h + w->childSpacing;
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

    std::visit([&](const auto& data) {
        using T = std::decay_t<decltype(data)>;

        if constexpr (std::is_same_v<T, PanelData>) {
            float cr = data.cornerRadius * scale;
            if (cr > 0.0f) {
                renderer2d.drawRoundedRect(b.x, b.y, b.w, b.h, cr,
                                            data.backgroundColor.r, data.backgroundColor.g,
                                            data.backgroundColor.b, data.backgroundColor.a);
            } else {
                renderer2d.drawFilledRect(b.x, b.y, b.w, b.h,
                                           data.backgroundColor.r, data.backgroundColor.g,
                                           data.backgroundColor.b, data.backgroundColor.a);
            }
        }
        else if constexpr (std::is_same_v<T, ButtonData>) {
            Color color = data.normalColor;
            if (w->isPressed) {
                color = data.pressedColor;
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

            // Center the label text. Pass pixelScale so each rasterized pixel
            // is scale x scale world units, which the shader zooms back to 1x1 screen pixels.
            if (!data.label.empty()) {
                float worldFontSize = data.fontSize * scale;
                Rect textBounds = BitmapFont::measureText(data.label, data.fontSize);
                // Scale the measured text bounds to world space for centering
                float textW = textBounds.w * scale;
                float textH = textBounds.h * scale;
                float textX = b.x + (b.w - textW) * 0.5f;
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
    }, w->data);

    // Render children -- for ScrollList, clip to visible window
    const bool isScrollList = std::holds_alternative<ScrollListData>(w->data);
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
