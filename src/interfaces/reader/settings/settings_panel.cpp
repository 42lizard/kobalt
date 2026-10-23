#include "interfaces/reader/ui/controls/buttons.h"
#include "interfaces/reader/ui/controls/icons.h"
#include "interfaces/reader/ui/controls/rows.h"
#include "interfaces/reader/ui/controls/text.h"
#include "interfaces/reader/ui/controls/options.h"
#include "interfaces/reader/ui/controls/panels.h"
#include "interfaces/reader/ui/dialogs/modals.h"
#include "interfaces/reader/ui/style/theme.h"
#include "settings_panel.h"
#include "support/strings.h"
#include "assets/font_catalog.h"
#include "interfaces/reader/ui/style/metrics.h"
#include "interfaces/reader/ui/style/private_text.h"
#include "interfaces/reader/ui/style/styles.h"
#include <QApplication>
#include <QButtonGroup>
#include <QGridLayout>
#include <QPointer>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>
#include <QHideEvent>
#include <QRegularExpression>

namespace kobalt::reader {
namespace m = ui::metrics;
namespace c = ui::metrics::chrome;
namespace styles = ui::styles;
using ui::readerStyle;
using ui::makeButton;
using ReaderOption = ui::DescribedOption;
namespace {
QPushButton *actionButton(QWidget *parent, QBoxLayout *layout, const char *name, const QString &text) {
    auto *button = new ui::Button(text, parent);
    button->setObjectName(name);
    button->setFocusPolicy(Qt::NoFocus);
    if (layout)
        layout->addWidget(button);
    return button;
}
}
SettingsPanel::SettingsPanel(QWidget *parent, const SettingsPanelInputs &inputs) : ui::Panel(parent, Qt::BottomEdge) {
    setObjectName("rrGeneralSettings");
    setSoftEdges(true);
    auto *generalRows = new QVBoxLayout(this);
    generalRows->setSizeConstraint(QLayout::SetNoConstraint);
    generalRows->setContentsMargins(0, 0, 0, 0);
    generalRows->setSpacing(0);
    auto *sections = new QWidget(this);
    generalRows->addWidget(sections, 1);
    auto *sectionRows = new QBoxLayout(QBoxLayout::LeftToRight, sections);
    sectionRows->setSizeConstraint(QLayout::SetNoConstraint);
    sectionRows->setObjectName("settingsSectionLayout");
    sectionRows->setContentsMargins(0, 0, 0, 0);
    sectionRows->setSpacing(0);
    tabRail = new QWidget(sections);
    tabRail->setObjectName("rrSettingsRail");
    auto *settingsTabs = new QBoxLayout(QBoxLayout::TopToBottom, tabRail);
    settingsTabs->setContentsMargins(0, 0, 0, 0);
    settingsTabs->setSpacing(0);
    sectionRows->addWidget(tabRail);
    auto *optionsColumn = new QWidget(sections);
    auto *optionsRows = new QVBoxLayout(optionsColumn);
    optionsRows->setContentsMargins(0, 0, 0, 0);
    optionsRows->setSpacing(0);
    optionsRows->setSizeConstraint(QLayout::SetNoConstraint);
    sectionRows->addWidget(optionsColumn, 1);
    optionsViewport = new ui::Panel(optionsColumn, Qt::LeftEdge);
    optionsViewport->setObjectName("rrSettingsViewport");
    optionsRows->addWidget(optionsViewport, 1);
    optionsClip = new QWidget(optionsViewport);
    optionsClip->setObjectName("rrSettingsPageClip");
    optionsPager = new ui::PagerBar(optionsColumn, "rrSettingsOptions");
    optionsPager->previous->setRule(ui::Edges(Qt::LeftEdge) | Qt::RightEdge);
    optionsPager->next->setRule(ui::Edges(Qt::LeftEdge) | Qt::RightEdge);
    optionsRows->addWidget(optionsPager);
    optionsPager->hide();
    connect(optionsPager->previous, &QPushButton::clicked, this, [this] {
        showOptionsPage(optionsPager->turnTarget(-1));
    });
    connect(optionsPager->next, &QPushButton::clicked, this, [this] {
        showOptionsPage(optionsPager->turnTarget(1));
    });
    settingsPages = new QStackedWidget(optionsClip);
    settingsPages->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    settingsPages->layout()->setSizeConstraint(QLayout::SetNoConstraint);
    settingsPages->setObjectName("rrSettingsPages");
    readerStyle(settingsPages, styles::tabFrame());
    auto *tabGroup = new QButtonGroup(this);
    const char *tabNames[] = {"rrSettingsDisplay",      "rrSettingsControls", "rrSettingsDefaults", "rrSettingsStyling",
                              "rrSettingsDictionaries", "rrSettingsSystem",   "rrSettingsAbout"};
    const ui::ControlIcon tabIcons[] = {
        ui::ControlIcon::Display,      ui::ControlIcon::Swipe,  ui::ControlIcon::Undo, ui::ControlIcon::Styling,
        ui::ControlIcon::Dictionaries, ui::ControlIcon::System, ui::ControlIcon::About};
    const QStringList tabs = {strings::get("settings.display"),      strings::get("settings.controls"),
                              strings::get("settings.defaults"),     strings::get("settings.styling"),
                              strings::get("settings.dictionaries"), strings::get("settings.system"),
                              strings::get("settings.about")};
    for (int index = 0; index < tabs.size(); ++index) {
        auto *button = makeButton(this, settingsTabs, tabNames[index], tabs[index], [this, index] {
            settingsPages->setCurrentIndex(index);
        });
        button->setProperty("reader_tab_label", tabs[index]);
        button->setProperty("reader_tab_icon", int(tabIcons[index]));
        button->setCheckable(true);
        tabGroup->addButton(button, index);
        settingsTabButtons.append(button);
        readerStyle(button, styles::tab(index == 0));
        // Qt 5.2 ignores a font set in the :checked rule. A property rule
        // applies on both Qt versions once the style is polished again.
        connect(button, &QPushButton::toggled, button, [button](bool on) {
            button->setProperty("kobalt_selected", on);
            button->style()->unpolish(button);
            button->style()->polish(button);
        });
        settingsTabs->setStretch(index, 1);
        if (index == DictionariesSettings && !inputs.downloadableDictionaries) {
            button->setEnabled(false);
            button->hide();
            settingsTabs->setStretch(index, 0);
        }
    }
    auto section = [](QWidget *parent, QBoxLayout *rows, const char *name, const QString &caption) {
        auto *header = new ui::ListHeader(parent);
        header->setObjectName(name);
        header->setShaded(true);
        header->setCaptions(caption.toUpper());
        header->setFixedHeight(header->sizeHint().height());
        rows->addWidget(header);
        return header;
    };
    auto *displayPage = new QWidget(settingsPages);
    displayPage->setObjectName("rrDisplayOptions");
    settingsPages->addWidget(displayPage);
    auto *generalOptions = new QVBoxLayout(displayPage);
    generalOptions->setContentsMargins(m::settings::displayMargins);
    generalOptions->setSpacing(0);
    auto check = [](QWidget *parent, const char *name, const QString &text, const QString &description) {
        auto *box = new ReaderOption(text, description, parent);
        box->setObjectName(name);
        return box;
    };
    darkOption =
        check(displayPage, "rrDarkMode", strings::get("reader.dark_mode"), strings::get("settings.dark_description"));
    const bool animationsSupported = inputs.animationsSupported;
    animationOption = check(displayPage, "rrPageAnimations", strings::get("reader.enable_page_animations"),
                            animationsSupported ? strings::get("settings.animations_description")
                                                : strings::get("settings.unsupported_description"));
    animationOption->setEnabled(animationsSupported);
    bionicOption = check(displayPage, "rrBionicReading", strings::get("reader.bold_reading"),
                         strings::get("reader.bionic_description"));
    section(displayPage, generalOptions, "rrDisplayScreenSection", strings::get("settings.section_screen"));
    refreshButton = ui::makeCaptionButton(displayPage, nullptr, "rrScreenRefresh",
                                          strings::get("reader.refresh_the_screen"), [] {});
    generalOptions->addWidget(refreshButton);
    generalOptions->addWidget(animationOption);
    section(displayPage, generalOptions, "rrDisplayAppearanceSection", strings::get("settings.section_appearance"));
    for (auto *option : {darkOption, bionicOption})
        generalOptions->addWidget(option);
    section(displayPage, generalOptions, "rrDisplayGraphsSection", strings::get("settings.section_graphs"));
    denseGraphOption = check(displayPage, "rrDenseGraph", strings::get("reader.dense_graph"),
                             strings::get("settings.dense_graph_description"));
    generalOptions->addWidget(denseGraphOption);
    generalOptions->addStretch(1);
    auto *controlsPage = new QWidget(settingsPages);
    controlsPage->setObjectName("rrControlsOptions");
    settingsPages->addWidget(controlsPage);
    auto *controlsRows = new QVBoxLayout(controlsPage);
    controlsRows->setContentsMargins(0, 0, 0, 0);
    controlsRows->setSpacing(0);
    section(controlsPage, controlsRows, "rrControlsNavigationSection", strings::get("settings.section_navigation"));
    pageNavigationButton = ui::makeCaptionButton(controlsPage, nullptr, "rrPageNavigation",
                                                 strings::get("settings.page_navigation"), [] {});
    controlsRows->addWidget(pageNavigationButton);
    tapZonesButton =
        ui::makeCaptionButton(controlsPage, nullptr, "rrTapZones", strings::get("settings.tap_zones"), [] {});
    controlsRows->addWidget(tapZonesButton);
    linkIndicatorOption = check(controlsPage, "rrShowLinkIndicator", strings::get("reader.show_link_indicator"),
                                strings::get("reader.link_indicator_description"));
    controlsRows->addWidget(linkIndicatorOption);
    section(controlsPage, controlsRows, "rrControlsMenuSection", strings::get("settings.section_reading_menu"));
    menuViewButton = ui::makeCaptionButton(controlsPage, nullptr, "rrDefaultMenuView",
                                           strings::get("settings.default_menu_view"), [] {});
    controlsRows->addWidget(menuViewButton);
    section(controlsPage, controlsRows, "rrControlsNavigationBarSection",
            strings::get("settings.section_navigation_bar"));
    navigationBarButton = ui::makeCaptionButton(controlsPage, nullptr, "rrNavigationBarButtons",
                                                strings::get("settings.navigation_bar_buttons"), [] {});
    controlsRows->addWidget(navigationBarButton);
    minimalAnnotationOption =
        check(controlsPage, "rrMinimalAnnotationMode", strings::get("settings.minimal_annotation_mode"),
              strings::get("settings.minimal_annotation_mode_description"));
    minimalAnnotationOption->hide();
    section(controlsPage, controlsRows, "rrControlsLightSection", strings::get("settings.section_frontlight"));
    const bool lightSupported = inputs.lightSupported;
    edgeLightOption = check(controlsPage, "rrEdgeLight", strings::get("reader.edge_light"),
                            !lightSupported          ? strings::get("settings.unsupported_description")
                            : inputs.warmthSupported ? strings::get("settings.edge_light_description")
                                                     : strings::get("settings.edge_brightness_description"));
    edgeLightOption->setEnabled(lightSupported);
    controlsRows->addWidget(edgeLightOption);
    controlsRows->addStretch(1);
    auto *defaultsPage = new QWidget(settingsPages);
    defaultsPage->setObjectName("rrDefaultsOptions");
    settingsPages->addWidget(defaultsPage);
    auto *defaultsRows = new QVBoxLayout(defaultsPage);
    defaultsRows->setContentsMargins(m::settings::displayMargins);
    defaultsRows->setSpacing(0);
    section(defaultsPage, defaultsRows, "rrDefaultsNewBooksSection", strings::get("settings.section_new_books"));
    inheritTextLayoutOption = check(defaultsPage, "rrInheritTextLayout", strings::get("settings.inherit_text_layout"),
                                    strings::get("settings.inherit_text_layout_description"));
    defaultsRows->addWidget(inheritTextLayoutOption);
    inheritHeaderFooterOption =
        check(defaultsPage, "rrInheritHeaderFooter", strings::get("settings.inherit_header_footer"),
              strings::get("settings.inherit_header_footer_description"));
    defaultsRows->addWidget(inheritHeaderFooterOption);
    inheritTextLayoutOption->setChecked(true);
    inheritHeaderFooterOption->setChecked(true);
    section(defaultsPage, defaultsRows, "rrDefaultsGlobalFontSection",
            strings::get("settings.section_default_global_font"));
    defaultFontButton =
        ui::makeCaptionButton(defaultsPage, nullptr, "rrDefaultFont", strings::get("reader.default_font"), [] {});
    defaultsRows->addWidget(defaultFontButton);
    section(defaultsPage, defaultsRows, "rrDefaultsRestoreSection", strings::get("reader.restore_defaults"));
    auto *resetRow = kobalt::ui::horizontalRow();
    defaultsRows->addLayout(resetRow);
    resetRow->addStretch(1);
    resetDefaults = ui::makeCaptionButton(defaultsPage, resetRow, "rrRestoreDefaults",
                                          strings::get("reader.restore_appearance_defaults"), [this] {
                                              settingsPages->setCurrentIndex(DefaultsSettings);
                                              restoreDialog->fitCard();
                                              restoreDialog->show();
                                              restoreDialog->raise();
                                              restoreDialog->setFocus();
                                          });
    // A list action paints its own label; a text link's underline does not belong here.
    readerStyle(resetDefaults, styles::font(m::text::secondary));
    resetDefaults->setFixedHeight(m::about::actionHeight);
    resetRow->addStretch(1);
    defaultsRows->addStretch(1);
    auto *stylingPage = new QWidget(settingsPages);
    stylingPage->setObjectName("rrStylingOptions");
    settingsPages->addWidget(stylingPage);
    auto *stylingRows = new QVBoxLayout(stylingPage);
    stylingRows->setContentsMargins(0, 0, 0, 0);
    stylingRows->setSpacing(0);
    section(stylingPage, stylingRows, "rrFontsFamiliesSection", strings::get("settings.font_families"));
    serifFontButton =
        ui::makeCaptionButton(stylingPage, nullptr, "rrSerifFont", strings::get("settings.serif_font"), [] {});
    sansFontButton =
        ui::makeCaptionButton(stylingPage, nullptr, "rrSansFont", strings::get("settings.sans_font"), [] {});
    monoFontButton =
        ui::makeCaptionButton(stylingPage, nullptr, "rrMonoFont", strings::get("settings.mono_font"), [] {});
    for (auto *button : {serifFontButton, sansFontButton, monoFontButton})
        stylingRows->addWidget(button);
    section(stylingPage, stylingRows, "rrAdvancedContentSection", strings::get("settings.section_book_content"));
    hideNonlinearOption =
        check(stylingPage, "rrHideNonlinearFragments", strings::get("reader.hide_nonlinear_fragments"),
              strings::get("settings.hide_nonlinear_fragments_description"));
    stylingRows->addWidget(hideNonlinearOption);
    avoidWastefulStylingOption =
        check(stylingPage, "rrAvoidWastefulStyling", strings::get("settings.avoid_wasteful_styling"),
              strings::get("settings.avoid_wasteful_styling_description"));
    avoidWastefulStylingOption->setChecked(true);
    stylingRows->addWidget(avoidWastefulStylingOption);
    stylingRows->addStretch(1);
    auto *systemPage = new QWidget(settingsPages);
    systemPage->setObjectName("rrSystemOptions");
    settingsPages->addWidget(systemPage);
    auto *systemRows = new QVBoxLayout(systemPage);
    systemRows->setContentsMargins(0, 0, 0, 0);
    systemRows->setSpacing(0);
    section(systemPage, systemRows, "rrSystemReaderSection", strings::get("settings.section_reader"));
    automaticOpeningOption =
        check(systemPage, "rrAutomaticBookOpening", strings::get("settings.automatic_book_opening"),
              inputs.automaticBookOpeningSupported ? strings::get("settings.automatic_book_opening_description")
                                                   : strings::get("settings.unsupported_description"));
    automaticOpeningOption->setEnabled(inputs.automaticBookOpeningSupported);
    systemRows->addWidget(automaticOpeningOption);
    section(systemPage, systemRows, "rrSystemSleepSection", strings::get("settings.section_sleep"));
    sleepScreenButton =
        ui::makeCaptionButton(systemPage, nullptr, "rrSleepScreen", strings::get("settings.sleep_screen"), [] {});
    sleepScreenButton->setEnabled(inputs.sleepScreenSupported);
    if (!inputs.sleepScreenSupported)
        sleepScreenButton->setCaption(strings::get("settings.unsupported_caption"));
    systemRows->addWidget(sleepScreenButton);
    section(systemPage, systemRows, "rrSystemInterfaceSection", strings::get("settings.section_user_interface"));
    uiFontButton = ui::makeCaptionButton(systemPage, nullptr, "rrUiFont", strings::get("settings.ui_font"), [] {});
    uiMonoFontButton =
        ui::makeCaptionButton(systemPage, nullptr, "rrUiMonoFont", strings::get("settings.ui_mono_font"), [] {});
    headerFooterFontButton = ui::makeCaptionButton(systemPage, nullptr, "rrHeaderFooterFont",
                                                   strings::get("settings.header_footer_font"), [] {});
    systemRows->addWidget(uiFontButton);
    systemRows->addWidget(uiMonoFontButton);
    systemRows->addWidget(headerFooterFontButton);
    headerFooterWeightButton = ui::makeCaptionButton(systemPage, nullptr, "rrHeaderFooterWeight",
                                                     strings::get("settings.header_footer_weight"), [] {});
    systemRows->addWidget(headerFooterWeightButton);
    hideTabLabelsOption = check(systemPage, "rrHideTabLabels", strings::get("settings.hide_tab_labels"),
                                strings::get("settings.hide_tab_labels_description"));
    systemRows->addWidget(hideTabLabelsOption);
    systemRows->addStretch(1);
    auto *aboutPage = new QWidget(settingsPages);
    aboutPage->setObjectName("rrAbout");
    settingsPages->addWidget(aboutPage);
    auto *aboutLayout = new QGridLayout(aboutPage);
    aboutLayout->setContentsMargins(m::about::margins);
    auto *aboutContent = new QWidget(aboutPage);
    aboutContent->setObjectName("rrAboutContent");
    aboutLayout->addWidget(aboutContent, 0, 0, Qt::AlignCenter);
    auto *aboutRows = new QVBoxLayout(aboutContent);
    aboutRows->setContentsMargins(0, 0, 0, 0);
    aboutRows->setSpacing(m::about::gap);
    auto *logo = new kobalt::ui::BrandIcon(aboutPage);
    logo->setObjectName("rrAboutLogo");
    logo->setFixedSize(m::about::logoSize, m::about::logoSize);
    aboutRows->addWidget(logo, 0, Qt::AlignHCenter);
    auto *wordmark = new ui::InterfaceLabel(strings::get("app.wordmark"), aboutPage);
    wordmark->setObjectName("rrAboutWordmark");
    wordmark->setAlignment(Qt::AlignCenter);
    readerStyle(wordmark, styles::brand(m::about::wordmarkText));
    aboutRows->addWidget(wordmark);
    auto *build = new QVBoxLayout;
    build->setSpacing(m::about::buildGap);
    aboutRows->addLayout(build);
    // "Version 0.4 (Qt6)", then the short commit with "*" for uncommitted changes.
    const auto release = inputs.release;
    auto *version = new ui::InterfaceLabel(strings::get("about.version").arg(release).arg(inputs.qtMajor), aboutPage);
    version->setObjectName("rrAboutVersion");
    version->setAlignment(Qt::AlignCenter);
    readerStyle(version, styles::font(m::text::secondary));
    build->addWidget(version);
    auto *commit = new ui::InterfaceLabel(inputs.commit, aboutPage);
    commit->setObjectName("rrAboutCommit");
    commit->setAlignment(Qt::AlignCenter);
    readerStyle(commit, styles::font(m::about::commitText));
    build->addWidget(commit);
    commit->setVisible(inputs.commitVisible);
    auto *description = new ui::InterfaceLabel(strings::get("about.description"), aboutPage);
    aboutDescription = description;
    description->setObjectName("rrAboutDescription");
    description->setWordWrap(true);
    description->setAlignment(Qt::AlignCenter);
    description->setFixedWidth(m::about::textWidth);
    readerStyle(description, styles::font(m::text::body));
    description->ensurePolished();
    // Measure at the displayed width so Qt cannot clip wrapped lines.
    description->setFixedHeight(description->heightForWidth(m::about::textWidth));
    aboutRows->addWidget(description, 0, Qt::AlignHCenter);
    aboutRows->addSpacing(m::about::actionGap / 2);
    auto *aboutActions = new QGridLayout;
    aboutActions->setSpacing(m::about::actionGap);
    aboutRows->addLayout(aboutActions);
    aboutActions->setColumnStretch(0, 1);
    aboutActions->setColumnStretch(2, 1);
    int actionRow = 0;
    auto textAction = [&](const char *name, const QString &label) {
        auto *row = kobalt::ui::horizontalRow();
        aboutActions->addLayout(row, actionRow++, 1);
        row->addStretch(1);
        auto *button = actionButton(aboutPage, row, name, label);
        readerStyle(button, styles::textAction() + styles::font(m::text::body));
        button->setFixedHeight(m::about::actionHeight);
        row->addStretch(1);
        return button;
    };
    openAcknowledgments = textAction("rrOpenAcknowledgments", strings::get("acknowledgments.acknowledgments"));
    if (inputs.debugProgressAvailable) {
        auto *debugRow = kobalt::ui::horizontalRow();
        aboutLayout->addLayout(debugRow, 0, 0, Qt::AlignRight | Qt::AlignBottom);
        debugProgress = actionButton(aboutPage, debugRow, "rrDebugProgress", {});
        const auto label = strings::get("reader.debug_progress");
        debugProgress->setAccessibleName(label);
        debugProgress->setToolTip(label);
        kobalt::ui::iconAction(debugProgress, ui::ControlIcon::Debug, label);
        debugProgress->setFixedWidth(m::control::iconButtonWidth);
        readerStyle(debugProgress,
                    QString("padding:0;border:none;min-width:%1px;max-width:%1px;min-height:%1px;max-height:%1px;")
                        .arg(m::control::stepSize));
        debugProgress->setFixedSize(m::control::stepSize, m::control::stepSize);
    }
    dictionaries = new DictionariesPage(settingsPages);
    settingsPages->insertWidget(DictionariesSettings, dictionaries);
    restoreDialog =
        new ui::ChoiceModal(parent,
                            {strings::get("reader.your_reading_position_stays_the_same_the_defaults_apply_when_you_c"),
                             strings::get("reader.restore_appearance_defaults_prompt"),
                             {}},
                            {strings::get("common.cancel"), "rrKeepDefaults"},
                            {strings::get("reader.restore_defaults"), "rrConfirmDefaults"});
    restoreDialog->setObjectName("rrRestoreConfirmation");
    restoreDialog->setDimmed(true);
    restoreDialog->card()->findChild<QLabel *>("rrModalTitle")->setObjectName("rrRestoreTitle");
    restoreDialog->descriptionLabel()->setObjectName("rrRestoreDescription");
    keepDefaults = restoreDialog->secondaryButton();
    confirmDefaults = restoreDialog->primaryButton();
    connect(keepDefaults, &QPushButton::clicked, this, [this] {
        dismissRestore();
    });
    connect(confirmDefaults, &QPushButton::clicked, this, [this] {
        dismissRestore();
    });
    restoreDialog->dismissed = [this] {
        dismissRestore();
    };
    if (inputs.desktopUi) {
        auto *nativePage = new QWidget(settingsPages);
        nativePage->setObjectName("rrNativeUiOptions");
        settingsPages->addWidget(nativePage);
        auto *rows = new QVBoxLayout(nativePage);
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setSpacing(0);
        hideStatusOption = check(nativePage, "rrHideStatusBar", strings::get("settings.hide_status_bar"),
                                 strings::get("settings.hide_status_bar_description"));
        section(nativePage, rows, "rrUiWindowSection", strings::get("settings.section_window"));
        rows->addWidget(hideStatusOption);
        rows->addStretch(1);
        auto *button = makeButton(this, nullptr, "rrSettingsUI", strings::get("settings.ui"), [this] {
            settingsPages->setCurrentIndex(NativeUiSettings);
        });
        button->setProperty("reader_tab_label", strings::get("settings.ui"));
        button->setProperty("reader_tab_icon", int(ui::ControlIcon::Desktop));
        button->setCheckable(true);
        tabGroup->addButton(button);
        settingsTabButtons.append(button);
        settingsTabs->insertWidget(StylingSettings + 1, button);
        readerStyle(button, styles::tab(false));
        connect(button, &QPushButton::toggled, button, [button](bool on) {
            button->setProperty("kobalt_selected", on);
            button->style()->unpolish(button);
            button->style()->polish(button);
        });
    }
    settingsError = new ui::InterfaceLabel(this);
    settingsError->setWordWrap(true);
    settingsError->hide();
    generalRows->addWidget(settingsError);
    connect(settingsPages, &QStackedWidget::currentChanged, this, [this](int index) {
        settingsTabButtons[index]->setChecked(true);
        optionPage = 0;
    });
    settingsTabButtons.first()->setChecked(true);
    QVector<ui::DescribedOption *> options = {avoidWastefulStylingOption,
                                              hideNonlinearOption,
                                              linkIndicatorOption,
                                              darkOption,
                                              bionicOption,
                                              animationOption,
                                              inheritTextLayoutOption,
                                              edgeLightOption,
                                              inheritHeaderFooterOption,
                                              denseGraphOption,
                                              hideTabLabelsOption,
                                              automaticOpeningOption,
                                              minimalAnnotationOption};
    if (inputs.desktopUi)
        options << hideStatusOption;
    for (auto *option : options) {
        auto presentation = option->presentation();
        presentation.indicator = ui::DescribedOption::Indicator::Square;
        presentation.pressedFill = true;
        presentation.dividers = Qt::BottomEdge;
        presentation.horizontalInset = c::inset;
        presentation.labelPixels = c::labelText;
        presentation.descriptionPixels = c::descriptionText;
        presentation.indicatorSize = {c::checkSize, c::checkSize};
        presentation.minimumRowHeight = c::optionHeight;
        option->setPresentation(presentation);
    }
    generalOptions->setContentsMargins(0, 0, 0, 0);
    defaultsRows->setContentsMargins(0, 0, 0, 0);
    resetRow->setStretch(0, 0);
    resetRow->setStretch(1, 1);
    resetRow->setStretch(2, 0);
    for (auto *button :
         {resetDefaults, refreshButton, sleepScreenButton, pageNavigationButton, tapZonesButton, navigationBarButton,
          menuViewButton, uiFontButton, uiMonoFontButton, headerFooterFontButton, headerFooterWeightButton,
          defaultFontButton, serifFontButton, sansFontButton, monoFontButton}) {
        button->setRule(Qt::BottomEdge);
        // The refresh row's caption shows its current choice, set when Settings opens.
        if (button == resetDefaults)
            button->setCaption(strings::get("settings.asks_first"));
        ui::readerStyle(
            button,
            QString(
                "QPushButton{font-size:%1px;min-height:%2px;max-height:%2px;border:0;border-radius:0;text-align:left;padding:0 %3px;background:palette(window);color:palette(window-text);} QPushButton:pressed{background:palette(midlight);}")
                .arg(c::labelText)
                .arg(c::listActionHeight)
                .arg(c::inset));
        button->setFixedHeight(c::listActionHeight);
    }
    settingsTabs->setAlignment(Qt::AlignTop);
    for (auto *button : settingsTabButtons) {
        button->setText(button->property("reader_tab_label").toString().toUpper());
        button->setFixedHeight(c::railRowHeight);
        if (auto *tab = dynamic_cast<ui::Button *>(button); tab && button->property("reader_tab_icon").isValid()) {
            ui::buttonIcon(button, ui::ControlIcon(button->property("reader_tab_icon").toInt()));
            button->setIconSize(QSize(c::tabIcon, c::tabIcon));
            tab->setIconLabelGap(c::tabGap);
            tab->setIconAboveLabel(true);
        }
        ui::readerStyle(
            button,
            QString(
                "QPushButton{font-size:%1px;min-height:%2px;max-height:%2px;border:0;border-bottom:%5px solid %3;border-right:%5px solid palette(window-text);border-radius:0;text-align:left;padding:0 %4px;background:palette(window);color:palette(window-text);} QPushButton:checked,QPushButton:pressed{background:palette(window-text);color:palette(window);}")
                .arg(c::tabText)
                .arg(c::railRowHeight)
                .arg(c::dividerLight)
                .arg(c::railInset)
                .arg(m::control::border));
    }
    fitRail();
    ui::readerStyle(
        settingsPages,
        QString("QStackedWidget{border:0;border-left:%1px solid palette(window-text);}").arg(m::control::border));
    ui::readerStyle(description, ui::styles::font(c::labelText));
    ui::readerStyle(version, ui::styles::font(c::descriptionText));
    ui::readerStyle(commit, ui::styles::font(ui::px(21)));
    ui::readerStyle(openAcknowledgments, ui::styles::textAction() + ui::styles::font(c::labelText));
    hide();
}
int SettingsPanel::fitContent(QSize availableSize) {
    fitRail(availableSize.height());
    const int fullWidth = qMax(1, availableSize.width() - tabRail->width());
    const int availableHeight = qMax(1, availableSize.height());
    auto *content = settingsPages->currentWidget();
    content->ensurePolished();
    auto *rows = content->layout();
    const auto measure = [&](int width) {
        aboutDescription->setFixedWidth(qMax(1, qMin<int>(m::about::textWidth, width - ui::px(32))));
        aboutDescription->setMinimumHeight(0);
        aboutDescription->setMaximumHeight(QWIDGETSIZE_MAX);
        aboutDescription->setFixedHeight(aboutDescription->heightForWidth(aboutDescription->width()));
        // Update the nested layout before reading the outer page's cached minimum.
        aboutDescription->parentWidget()->layout()->activate();
        return qMax(rows->minimumSize().height(),
                    rows->hasHeightForWidth() ? rows->totalHeightForWidth(width) : rows->sizeHint().height());
    };
    const bool dictionaries = settingsPages->currentIndex() == DictionariesSettings;
    settingsPages->ensurePolished();
    const auto frame = settingsPages->contentsMargins();
    const int contentWidth = qMax(1, fullWidth - frame.left() - frame.right());
    const int contentHeight = dictionaries ? availableHeight : measure(contentWidth);
    const bool overflow = contentHeight > availableHeight;
    const int panelHeight = availableHeight;
    pageHeight = qMax(1, panelHeight - (overflow ? optionsPager->height() : 0));
    optionsViewport->setFixedHeight(pageHeight);
    settingsPages->setFixedSize(fullWidth, qMax(contentHeight, pageHeight));
    settingsPages->layout()->activate();
    content->setGeometry(settingsPages->contentsRect());
    rows->setGeometry(content->rect());
    optionPages.clear();
    int start = 0, sectionStart = -1;
    bool occupied = false;
    if (overflow) {
        for (int index = 0; index < rows->count(); ++index) {
            auto *item = rows->itemAt(index);
            if (item->isEmpty())
                continue;
            auto *row = item->widget();
            const auto geometry = item->geometry().translated(content->pos());
            if (row && dynamic_cast<ui::ListHeader *>(row)) {
                if (sectionStart < 0)
                    sectionStart = geometry.top();
                continue;
            }
            const int top = sectionStart >= 0 ? sectionStart : geometry.top();
            const int bottom = geometry.bottom() + 1;
            // Keep section headings with their first option and avoid partial rows.
            if (bottom - start > pageHeight && top > start && occupied) {
                optionPages.append({start, top});
                start = top;
            }
            // Unusually tall content still needs every part to be reachable.
            while (bottom - start > pageHeight) {
                optionPages.append({start, start + pageHeight});
                start += pageHeight;
            }
            sectionStart = -1;
            occupied = true;
        }
    }
    optionPages.append({start, overflow ? qMax(start + 1, contentHeight) : pageHeight});
    showOptionsPage(optionPage);
    return panelHeight;
}
void SettingsPanel::showOptionsPage(int page) {
    if (optionPages.isEmpty())
        return;
    optionPage = qBound(0, page, int(optionPages.size()) - 1);
    const auto range = optionPages[optionPage];
    optionsClip->setGeometry(0, 0, settingsPages->width(), qMin(pageHeight, range.second - range.first));
    settingsPages->move(0, -range.first);
    optionsPager->setPage(optionPage, optionPages.size());
}
SettingsPanel::~SettingsPanel() {
    delete restoreDialog;
    delete fontDialog;
}
void SettingsPanel::showFontChoices(const QString &title, const QVector<ReadingFont> &fonts, const QString &current,
                                    const QString &defaultFamily, std::function<void(const QString &)> chosen,
                                    std::function<void(const QStringList &)> prepareFonts) {
    if (fontDialog || fonts.isEmpty())
        return;
    fontDialog = new ui::ModalOverlay(parentWidget());
    fontDialog->setObjectName("rrSettingsFontPopup");
    fontDialog->setDimmed(true);
    fontDialog->setGeometry(parentWidget()->rect());
    fontDialog->dismissed = [this] {
        dismissFontChoices();
    };
    auto *card = fontDialog->card();
    auto *rows = card->rows();
    rows->setContentsMargins(m::control::border, m::notice::cardMargins.top(), m::control::border, m::control::border);
    rows->setSpacing(0);
    auto *heading = card->title(title, "rrSettingsFontTitle");
    heading->setContentsMargins(c::inset, 0, c::inset, m::notice::cardMargins.bottom());
    auto *section = new ui::ListHeader(card);
    section->setObjectName("rrSettingsFontSection");
    section->setShaded(true);
    section->setFixedHeight(section->sizeHint().height());
    rows->addWidget(section);
    auto *choices = new ui::RadioList(card);
    choices->setObjectName("rrSettingsFontChoices");
    choices->setMinimumRowHeight(m::list::oneLine);
    choices->setTopDivider(true);
    int selected = 0;
    for (int index = 0; index < fonts.size(); ++index) {
        const auto &family = fonts[index].family;
        auto *option = choices->add(
            QString("rrSettingsFontChoice%1").arg(index),
            family.isEmpty() ? strings::get("settings.font_default_value").arg(fontDisplayName(defaultFamily))
                             : fontDisplayName(family));
        option->setProperty("reader_font_family", family);
        if (family == current)
            selected = index;
    }
    choices->setCurrent(selected);
    rows->addWidget(choices);
    auto *pager = new ui::PagerBar(card, "rrSettingsFont");
    rows->addWidget(pager);
    const int cardWidth = qMin<int>(m::notice::cardWidth, parentWidget()->width() - 2 * m::notice::sideClearance);
    const int rowHeight = choices->rowHeight(cardWidth - 2 * m::control::border);
    heading->ensurePolished();
    const int overhead = rows->contentsMargins().top() + rows->contentsMargins().bottom() +
                         heading->sizeHint().height() + section->height() + pager->height();
    const int perPage = qMax(1, (parentWidget()->height() - 2 * m::notice::sideClearance - overhead) / rowHeight);
    QVector<QPair<int, int>> pages;
    for (int first = 0; first < fonts.size();) {
        int end = first + 1;
        while (end < fonts.size() && end - first < perPage && fonts[end].origin == fonts[first].origin)
            ++end;
        pages.append({first, end - first});
        first = end;
    }
    const auto showPage = [this, choices, pager, section, card, cardWidth, rowHeight, pages, fonts,
                           prepareFonts](int page) {
        page = qBound(0, page, int(pages.size()) - 1);
        const auto range = pages[page];
        QStringList families;
        for (int index = range.first; index < range.first + range.second; ++index)
            if (!fonts[index].family.isEmpty())
                families.append(fonts[index].family);
        if (prepareFonts)
            prepareFonts(families);
        for (int index = range.first; index < range.first + range.second; ++index) {
            if (fonts[index].family.isEmpty())
                continue;
            auto *option = choices->options()[index];
            const auto sample = ui::fontSample(fonts[index].family);
            option->setFont(sample);
            auto look = option->presentation();
            look.labelPixels = sample.pixelSize();
            look.opticalLabel = true;
            option->setPresentation(look);
            auto family = sample.family();
            family.replace('\\', "\\\\");
            family.replace('"', "\\\"");
            readerStyle(
                option,
                QString("font-family:\"%1\";font-size:%2px;font-weight:400;").arg(family).arg(sample.pixelSize()));
        }
        QString caption;
        switch (fonts[range.first].origin) {
        case FontOrigin::Recommended:
            caption = strings::get("reader.recommended_fonts");
            break;
        case FontOrigin::BuiltIn:
            caption = strings::get("reader.built_in_fonts");
            break;
        case FontOrigin::Sideloaded:
            caption = strings::get("reader.sideloaded_fonts");
            break;
        case FontOrigin::Interface:
            caption = strings::get("reader.supplemental_fonts");
            break;
        }
        section->setCaptions(caption.toUpper());
        choices->showRows(range.first, range.second);
        choices->setFixedHeight(range.second * rowHeight);
        pager->setPage(page, pages.size());
        card->fitToWidth(cardWidth);
        fontDialog->centerCard();
    };
    connect(pager->previous, &QPushButton::clicked, fontDialog, [pager, showPage] {
        showPage(pager->turnTarget(-1));
    });
    connect(pager->next, &QPushButton::clicked, fontDialog, [pager, showPage] {
        showPage(pager->turnTarget(1));
    });
    choices->chosen = [this, fonts, chosen](int index) {
        const auto family = fonts[index].family;
        dismissFontChoices();
        chosen(family);
    };
    int initialPage = 0;
    for (int page = 0; page < pages.size(); ++page)
        if (selected >= pages[page].first && selected < pages[page].first + pages[page].second)
            initialPage = page;
    showPage(initialPage);
    fontDialog->show();
    fontDialog->raise();
    fontDialog->setFocus();
}
void SettingsPanel::showHeaderFooterWeights(int current, const QFont &sampleFont, std::function<void(int)> chosen) {
    if (fontDialog)
        return;
    fontDialog = new ui::ModalOverlay(parentWidget());
    fontDialog->setObjectName("rrHeaderFooterWeightPopup");
    fontDialog->setDimmed(true);
    fontDialog->setGeometry(parentWidget()->rect());
    fontDialog->dismissed = [this] {
        dismissFontChoices();
    };
    auto *card = fontDialog->card();
    auto *rows = card->rows();
    rows->setContentsMargins(m::control::border, m::notice::cardMargins.top(), m::control::border, m::control::border);
    rows->setSpacing(0);
    auto *heading = card->title(strings::get("settings.header_footer_weight"), "rrHeaderFooterWeightTitle");
    heading->setContentsMargins(c::inset, 0, c::inset, m::notice::cardMargins.bottom());
    auto *choices = new ui::RadioList(card);
    choices->setMinimumRowHeight(m::list::oneLine);
    choices->setTopDivider(true);
    for (int steps : {0, 2, 4, 6, 8}) {
        auto *option = choices->add(QString("rrHeaderFooterWeight%1").arg(steps),
                                    steps ? strings::get("settings.header_footer_weight_heavier").arg(steps)
                                          : strings::get("settings.header_footer_weight_lighter"));
        ui::DescribedOption::ValueStyle sample;
        sample.sampleFont = sampleFont;
        sample.sampleWeight = ui::interfaceWeight() + steps;
        option->setValue(QStringLiteral("SAMPLE TEXT"), sample);
        if (steps == current)
            choices->setCurrent(choices->options().size() - 1);
    }
    choices->chosen = [this, chosen](int index) {
        dismissFontChoices();
        chosen(index * 2);
    };
    rows->addWidget(choices);
    const int cardWidth = qMin<int>(m::notice::cardWidth, parentWidget()->width() - 2 * m::notice::sideClearance);
    choices->setFixedHeight(choices->heightForWidth(cardWidth - 2 * m::control::border));
    card->fitToWidth(cardWidth);
    fontDialog->centerCard();
    fontDialog->show();
    fontDialog->raise();
    fontDialog->setFocus();
}
void SettingsPanel::dismissFontChoices() {
    if (!fontDialog)
        return;
    fontDialog->hide();
    fontDialog->deleteLater();
    fontDialog.clear();
}
void SettingsPanel::dismissRestore() {
    if (!restoreDialog || restoreDialog->isHidden())
        return;
    restoreDialog->hide();
    resetDefaults->setFocus();
}
void SettingsPanel::hideEvent(QHideEvent *event) {
    dismissRestore();
    dismissFontChoices();
    ui::Panel::hideEvent(event);
}
void SettingsPanel::resizeEvent(QResizeEvent *event) {
    // A rotation can leave too few rows for the current font page. Reopen at the new size.
    dismissFontChoices();
    ui::Panel::resizeEvent(event);
}
void SettingsPanel::showDictionaries(bool visible) {
    auto *button = settingsTabButtons[DictionariesSettings];
    button->setVisible(visible);
    button->setEnabled(visible);
    if (!visible && settingsPages->currentIndex() == DictionariesSettings)
        settingsPages->setCurrentIndex(DisplaySettings);
    fitRail();
}
void SettingsPanel::showNativeUi(bool visible) {
    if (auto *button = settingsTabButtons.value(NativeUiSettings, nullptr)) {
        button->setVisible(visible);
        button->setEnabled(visible);
        if (!visible && settingsPages->currentIndex() == NativeUiSettings) {
            settingsPages->setCurrentIndex(DisplaySettings);
        }
        fitRail();
    }
}

void SettingsPanel::fitRail(int availableHeight) {
    // As wide as the widest visible label, or the icon above it, with the
    // rail's margin on each side. Without Dictionaries the rail narrows.
    int visible = 0;
    for (auto *button : settingsTabButtons)
        if (!button->isHidden())
            ++visible;
    // The rail uses the full panel height, independently of the list pager.
    const int rowHeight = availableHeight > 0 ? qMin<int>(c::railRowHeight, qMax(1, availableHeight) / qMax(1, visible))
                                              : int(c::railRowHeight);
    int widest = c::tabIcon;
    for (auto *button : settingsTabButtons)
        if (!button->isHidden()) {
            if (button->height() != rowHeight) {
                auto style = button->property("reader_style").toString();
                // Style minimums must agree with the fitted height on Qt 5.2 too.
                style.replace(QRegularExpression("(min-height|max-height):[0-9]+px"),
                              "\\1:" + QString::number(rowHeight) + "px");
                ui::readerStyle(button, style);
                button->setFixedHeight(rowHeight);
                button->setIconSize(
                    QSize(c::tabIcon, qMax(1, qMin<int>(c::tabIcon, rowHeight - c::tabText - c::tabGap - ui::px(8)))));
            }
            button->ensurePolished();
            widest = qMax(widest, ui::interfaceTextWidth(button->text(), button->font()));
        }
    tabRail->setFixedWidth(widest + 2 * c::railInset);
}

}
