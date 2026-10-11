#include "interfaces/reader/ui/controls/buttons.h"
#include "interfaces/reader/ui/controls/icons.h"
#include "interfaces/reader/ui/controls/rows.h"
#include "interfaces/reader/ui/controls/sliders.h"
#include "interfaces/reader/ui/style/theme.h"
#include "appearance_panel.h"
#include "support/strings.h"
#include "reading/appearance_settings.h"
#include "reading/reading_density.h"
#include "interfaces/reader/ui/style/styles.h"

namespace kobalt::reader {
namespace m = ui::metrics;
namespace styles = ui::styles;
using ui::readerStyle;
using ui::buttonIcon;
using ui::ControlIcon;
using ui::FontButton;
AppearancePanel::AppearancePanel(const AppearanceLayout &layout, const AppearancePanelInputs &inputs) {
    auto *typography = layout.typography, *pageLayout = layout.pageLayout;
    auto *typeRows = layout.typeRows, *pageRows = layout.pageRows;
    fontButton = new FontButton(typography);
    fontButton->setObjectName("rrFont");
    readerStyle(
        fontButton,
        QString("min-height:%1px;max-height:%1px;").arg(m::choices::fontButtonHeight - 2 * m::control::padding));
    typeRows->addWidget(fontButton);
    typeRows->addSpacing(m::control::fontPickerGap);
    // Text size counts half points and margins count 300 ppi pixels, so both
    // look the same on every screen; the engine gets device pixels.
    sizeSlider =
        kobalt::ui::sliderRow(typography, typeRows, "rrTextSize", "rrTextSizeValue", strings::get("reader.text_size"),
                              "rrSmaller", "rrLarger", kobalt::minimumHalfPoints, kobalt::maximumHalfPoints, 1, {});
    opticalSizeSlider =
        ui::sliderRow(typography, typeRows, "rrOpticalSize", "rrOpticalSizeValue", strings::get("reader.optical_size"),
                      "rrOpticalSizeLess", "rrOpticalSizeMore", 6, 72, 1, {});
    opticalSizeSlider->parentWidget()->hide();
    weightSlider =
        kobalt::ui::sliderRow(typography, typeRows, "rrFontWeight", "rrFontWeightValue",
                              strings::get("reader.font_thickness"), "rrLighter", "rrHeavier", 0, 8, 1, {}, "+");
    variableWeightSlider =
        ui::sliderRow(typography, typeRows, "rrVariableFontWeight", "rrVariableFontWeightValue",
                      strings::get("reader.font_weight"), "rrVariableLighter", "rrVariableHeavier", 300, 650, 10, {});
    variableWeightSlider->parentWidget()->hide();
    spacingSlider = kobalt::ui::sliderRow(typography, typeRows, "rrLineSpacing", "rrLineSpacingValue",
                                          strings::get("reader.line_spacing"), "rrTighter", "rrLooser", 100,
                                          maximumLineSpacing, 1, {}, {}, "%");
    spacingSlider->setMarkers({100, 110, 120, 130, 140, 150, 175, 200, 250, 300}, ui::SettingSlider::MarkerMode::Stops);
    marginSlider = kobalt::ui::sliderRow(pageLayout, pageRows, "rrMargins", "rrMarginsValue",
                                         strings::get("reader.margins"), "rrNarrower", "rrWider", 0, 120, 3, {});
    verticalMarginSlider = kobalt::ui::sliderRow(pageLayout, pageRows, "rrVerticalMargins", "rrVerticalMarginsValue",
                                                 strings::get("reader.vertical_margins"), "rrVerticalMarginsLess",
                                                 "rrVerticalMarginsMore", 0, 120, 3, {});
    typeRows->addSpacing(m::control::sectionGap);
    kobalt::ui::SegmentedRow alignment(typography, typeRows, "rrAlignment", strings::get("reader.alignment"));
    auto alignmentButton = [&](const char *name, const char *value, const QString &label, ControlIcon icon) {
        auto *button = alignment.add(name);
        button->setCheckable(true);
        button->setProperty("alignment", value);
        button->setAccessibleName(label);
        button->setToolTip(label);
        if (QString::fromLatin1(value) == "publisher")
            button->setText(strings::get("common.auto"));
        else {
            buttonIcon(button, icon);
            button->setIconSize(QSize(m::control::chapterIconSize, m::control::chapterIconSize));
        }
        alignmentButtons.append(button);
    };
    alignmentButton("rrAlignLeft", "left", strings::get("reader.align_left"), ControlIcon::AlignLeft);
    alignmentButton("rrAlignJustify", "justify", strings::get("reader.justify"), ControlIcon::AlignJustify);
    alignmentButton("rrAlignPublisher", "publisher", strings::get("reader.auto_alignment"), ControlIcon::Layout);
    kobalt::ui::SegmentedRow hyphenation(typography, typeRows, "rrHyphenation", strings::get("reader.hyphenation"));
    for (const auto *mode : {"off", "on", "auto"}) {
        const QString value = QString::fromLatin1(mode);
        const auto label = value == "off"  ? strings::get("common.off")
                           : value == "on" ? strings::get("common.on")
                                           : strings::get("common.auto");
        auto *button = hyphenation.add("rrHyphenation_" + value, label);
        button->setProperty("hyphenation", value);
        button->setCheckable(true);
        hyphenButtons.append(button);
    }
    if (!inputs.systemRotation) {
        kobalt::ui::SegmentedRow rotation(pageLayout, pageRows, "rrRotation", strings::get("reader.rotation"));
        const QStringList rotationNames = {strings::get("reader.portrait"), strings::get("reader.landscape"),
                                           strings::get("reader.portrait_upside_down"),
                                           strings::get("reader.landscape_upside_down")};
        const bool sideButtons = inputs.sideButtons;
        for (int index = 0; index < rotationNames.size(); ++index) {
            const int target = inputs.reverseLandscapeButtons && (index == 1 || index == 3) ? 4 - index : index;
            const int value = 1 << target;
            auto *button = rotation.add(QString("rrOrientation%1").arg(value));
            button->setProperty("orientation", value);
            button->setAccessibleName(rotationNames[index]);
            button->setToolTip(rotationNames[index]);
            rotationButtons.append(button);
            static_cast<ui::Button *>(button)->setIconConfiguration(
                {static_cast<ControlIcon>(int(ControlIcon::Portrait) + index), ui::IconRendition::OriginalAsset,
                 sideButtons});
            button->setIconSize(QSize(m::control::iconSize, m::control::iconSize));
        }
        autoRotateOption = rotation.add("rrAutoRotate", strings::get("common.auto"));
        autoRotateOption->setAccessibleName(strings::get("reader.automatic_rotation"));
        autoRotateOption->setVisible(inputs.automaticRotationAvailable);
    }
    auto chromeSizeRow = [this, pageLayout, pageRows, &inputs](bool footer) {
        const QString name = footer ? "rrFooterTextSize" : "rrHeaderTextSize";
        auto *row = kobalt::ui::labeledRow(pageLayout, pageRows,
                                           footer ? strings::get("reader.footer") : strings::get("reader.header"));
        auto *button = new kobalt::ui::DropdownButton(pageLayout);
        button->setObjectName(name);
        button->setMinimumWidth(m::control::captionWidth);
        button->setText(footer ? inputs.footerCaption : inputs.headerCaption);
        row->addWidget(button, 1);
        (footer ? footerTextSize : headerTextSize) = button;
    };
    chromeSizeRow(false);
    chromeSizeRow(true);
    kobalt::ui::SegmentedRow clock(pageLayout, pageRows, "rrClock", strings::get("reader.clock"));
    clockOption = clock.add("rrShowClock", strings::get("common.on"));
    clockHideOption = clock.add("rrHideClock", strings::get("common.off"));
    (inputs.showClock ? clockOption : clockHideOption)->setChecked(true);
}
}
