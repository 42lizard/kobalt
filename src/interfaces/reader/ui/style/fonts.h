#pragma once
#include "metrics.h"
#include <QFont>
#include <QJsonObject>

class QWidget;
namespace kobalt {
class AssetBundle;
}

namespace kobalt::ui {
// Consume verified bytes on the GUI thread before constructing reader UI.
// The registration cache owns its data and temporary copies, so Qt never
// needs the asset storage to remain mounted.
bool initializeInterfaceFonts(const AssetBundle &assets);
// Register bundled faces; never change Nickel's application font.
QFont interfaceFont(int pixels = -1);
QFont chromeFont(int pixels = -1);
QFont brandFont(int pixels, bool italic = false);
// Uppercase, spaced monospace for notes, counts and section captions.
QFont captionFont(int pixels = metrics::chrome::captionText);
inline QString interfaceFontFamily() {
    return interfaceFont().family();
}

// Interface text weight in steps of 1/256 em, the unit of the reading weight
// setting. Settings > Advanced adds three steps: IBM Plex Medium is too heavy.
inline constexpr int heavierTextSteps = 3;
inline int interfaceWeightSteps(const QJsonObject &preferences) {
    return preferences.value("heavier_interface_text").toBool(true) ? heavierTextSteps : 0;
}
// Replaces the registered faces with faces of this weight. Text drawn
// afterwards uses them; call refreshInterfaceText() for widgets already
// shown. Returns whether the faces changed. Before the first font is used,
// this only sets the weight to register.
bool setInterfaceWeight(int steps);
int interfaceWeight();
// Draws the widgets under `root` again with the current faces.
void refreshInterfaceText(QWidget *root);
// A copy of `font` that loads its faces again. Documents that keep a font
// renew it when they receive QEvent::FontChange.
QFont renewedFont(const QFont &font);
}
