#pragma once
#include "interfaces/reader_interface.h"
namespace kobalt {
std::unique_ptr<ReaderOpeningView> createOpening(QWidget *background, const QJsonObject &preferences,
                                                 PresentationFactory presentation);
// The standalone host sizes this loading view and handles system Back itself.
std::unique_ptr<ReaderOpeningView> createEmbeddedOpening(QWidget *parent, PresentationFactory presentation);
}
