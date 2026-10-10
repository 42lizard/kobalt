#include "reader_page_layout.h"
#include <lvdocview.h>
#include <QString>
#include <vector>

namespace kobalt {
namespace {
ldomNode *leadingImage(ldomNode *root) {
    if (!root)
        return nullptr;
    std::vector<ldomNode *> pending{root};
    while (!pending.empty()) {
        auto *node = pending.back();
        pending.pop_back();
        if (node->isText()) {
            if (!QString::fromUtf8(UnicodeToUtf8(node->getText()).c_str()).trimmed().isEmpty())
                return nullptr;
            continue;
        }
        if (!node->isElement())
            continue;
        if (node->isNodeName("img") || node->isNodeName("svg"))
            return node;
        if (node->isNodeName("head") || node->isNodeName("style") || node->isNodeName("script"))
            continue;
        if (node->isNodeName("hr") || node->isNodeName("br"))
            return nullptr;
        for (int i = node->getChildCount() - 1; i >= 0; --i)
            pending.push_back(node->getChildNode(i));
    }
    return nullptr;
}
void mark(ldomNode *root, lUInt16 attribute) {
    auto *image = leadingImage(root);
    if (!image)
        return;
    // Only a leading image with authored top spacing needs the body boundary.
    // Later illustrations keep their publisher layout.
    bool hasTopMargin = false;
    for (auto *node = image; node && leadingImage(node) == image; node = node->getParentNode()) {
        if (!node->isElement() || node->isNodeName("DocFragment"))
            break;
        hasTopMargin |= node->getStyle()->margin[2].value > 0;
        if (!node->isNodeName("body") || hasTopMargin)
            node->setAttributeValue(0, attribute, U"1");
        if (node->isNodeName("body"))
            break;
    }
}
}
void markChapterLeadingImages(LVDocView *view) {
    if (!view || !view->getDocument())
        return;
    auto *document = view->getDocument();
    const auto attribute = document->getAttrNameIndex("data-kobalt-leading-image");
    std::vector<ldomNode *> pending{document->getRootNode()}, bodies;
    while (!pending.empty()) {
        auto *node = pending.back();
        pending.pop_back();
        if (!node)
            continue;
        if (!node->isElement())
            continue;
        if (node->hasAttribute(attribute))
            node->setAttributeValue(0, attribute, U"");
        if (node->isNodeName("body"))
            bodies.push_back(node);
        for (int i = node->getChildCount() - 1; i >= 0; --i)
            pending.push_back(node->getChildNode(i));
    }
    for (auto *body : bodies)
        mark(body, attribute);
    // A contents entry can start a chapter partway through a spine file.
    // The view accessor paginates; inspect source anchors before initial layout.
    std::vector<LVTocItem *> chapters{document->getToc()};
    while (!chapters.empty()) {
        auto *chapter = chapters.back();
        chapters.pop_back();
        if (!chapter)
            continue;
        mark(chapter->getXPointer().getNode(), attribute);
        for (int i = chapter->getChildCount() - 1; i >= 0; --i)
            chapters.push_back(chapter->getChild(i));
    }
}
}
