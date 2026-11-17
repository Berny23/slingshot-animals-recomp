// Generated from Breeze's kstyle/breeze.kcfg (what kconfig_compiler makes, read-only).
#include "breezestyleconfigdata.h"

#include <KConfigGroup>

namespace Breeze {

StyleConfigData* StyleConfigData::self() {
    static StyleConfigData data;
    return &data;
}

StyleConfigData::StyleConfigData() : config_(KSharedConfig::openConfig(QStringLiteral("breezerc"))) { load(); }

void StyleConfigData::load() {
    config_->reparseConfiguration();
    shadowStrength_ = KConfigGroup(config_, QStringLiteral("Common")).readEntry("ShadowStrength", 255);
    {
        const QString v = KConfigGroup(config_, QStringLiteral("Common")).readEntry("ShadowSize", QString());
        shadowSize_ = ShadowLarge;
        if (v == QLatin1String("ShadowNone")) shadowSize_ = ShadowNone;
        if (v == QLatin1String("ShadowSmall")) shadowSize_ = ShadowSmall;
        if (v == QLatin1String("ShadowMedium")) shadowSize_ = ShadowMedium;
        if (v == QLatin1String("ShadowLarge")) shadowSize_ = ShadowLarge;
        if (v == QLatin1String("ShadowVeryLarge")) shadowSize_ = ShadowVeryLarge;
    }
    shadowColor_ = KConfigGroup(config_, QStringLiteral("Common")).readEntry("ShadowColor", QColor(0, 0, 0));
    outlineCloseButton_ = KConfigGroup(config_, QStringLiteral("Common")).readEntry("OutlineCloseButton", false);
    animationsEnabled_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("AnimationsEnabled", true);
    animationSteps_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("AnimationSteps", 10);
    animationsDuration_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("AnimationsDuration", 100);
    stackedWidgetTransitionsEnabled_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("StackedWidgetTransitionsEnabled", false);
    progressBarAnimated_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("ProgressBarAnimated", true);
    progressBarBusyStepDuration_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("ProgressBarBusyStepDuration", 800);
    scrollBarAddLineButtons_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("ScrollBarAddLineButtons", 0);
    scrollBarSubLineButtons_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("ScrollBarSubLineButtons", 0);
    {
        const QString v = KConfigGroup(config_, QStringLiteral("Style")).readEntry("MnemonicsMode", QString());
        mnemonicsMode_ = MN_AUTO;
        if (v == QLatin1String("MN_NEVER")) mnemonicsMode_ = MN_NEVER;
        if (v == QLatin1String("MN_AUTO")) mnemonicsMode_ = MN_AUTO;
        if (v == QLatin1String("MN_ALWAYS")) mnemonicsMode_ = MN_ALWAYS;
    }
    toolBarDrawItemSeparator_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("ToolBarDrawItemSeparator", true);
    viewDrawFocusIndicator_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("ViewDrawFocusIndicator", true);
    sliderDrawTickMarks_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("SliderDrawTickMarks", true);
    viewDrawTreeBranchLines_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("ViewDrawTreeBranchLines", true);
    viewInvertSortIndicator_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("ViewInvertSortIndicator", true);
    tabBarDrawCenteredTabs_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("TabBarDrawCenteredTabs", false);
    dockWidgetDrawFrame_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("DockWidgetDrawFrame", false);
    sidePanelDrawFrame_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("SidePanelDrawFrame", false);
    menuItemDrawStrongFocus_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("MenuItemDrawStrongFocus", true);
    {
        const QString v = KConfigGroup(config_, QStringLiteral("Style")).readEntry("WindowDragMode", QString());
        windowDragMode_ = WD_MINIMAL;
        if (v == QLatin1String("WD_NONE")) windowDragMode_ = WD_NONE;
        if (v == QLatin1String("WD_MINIMAL")) windowDragMode_ = WD_MINIMAL;
        if (v == QLatin1String("WD_FULL")) windowDragMode_ = WD_FULL;
    }
    windowDragWhiteList_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("WindowDragWhiteList", QStringList());
    windowDragBlackList_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("WindowDragBlackList", QStringList());
    splitterProxyEnabled_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("SplitterProxyEnabled", true);
    splitterProxyWidth_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("SplitterProxyWidth", 12);
    widgetExplorerEnabled_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("WidgetExplorerEnabled", false);
    drawWidgetRects_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("DrawWidgetRects", false);
    menuOpacity_ = KConfigGroup(config_, QStringLiteral("Style")).readEntry("MenuOpacity", 100);
}

}
