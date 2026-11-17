// Generated from Breeze's kstyle/breeze.kcfg (what kconfig_compiler makes, read-only).
#pragma once

#include <KSharedConfig>
#include <QColor>
#include <QStringList>

namespace Breeze {

class StyleConfigData {
public:
    enum { ShadowNone, ShadowSmall, ShadowMedium, ShadowLarge, ShadowVeryLarge };
    enum { MN_NEVER, MN_AUTO, MN_ALWAYS };
    enum { WD_NONE, WD_MINIMAL, WD_FULL };

    static StyleConfigData* self();
    KSharedConfig::Ptr sharedConfig() const { return config_; }
    void load();

    static int shadowStrength() { return self()->shadowStrength_; }
    static void setShadowStrength(int v) { self()->shadowStrength_ = v; }
    static int shadowSize() { return self()->shadowSize_; }
    static void setShadowSize(int v) { self()->shadowSize_ = v; }
    static QColor shadowColor() { return self()->shadowColor_; }
    static void setShadowColor(const QColor& v) { self()->shadowColor_ = v; }
    static bool outlineCloseButton() { return self()->outlineCloseButton_; }
    static void setOutlineCloseButton(bool v) { self()->outlineCloseButton_ = v; }
    static bool animationsEnabled() { return self()->animationsEnabled_; }
    static void setAnimationsEnabled(bool v) { self()->animationsEnabled_ = v; }
    static int animationSteps() { return self()->animationSteps_; }
    static void setAnimationSteps(int v) { self()->animationSteps_ = v; }
    static int animationsDuration() { return self()->animationsDuration_; }
    static void setAnimationsDuration(int v) { self()->animationsDuration_ = v; }
    static bool stackedWidgetTransitionsEnabled() { return self()->stackedWidgetTransitionsEnabled_; }
    static void setStackedWidgetTransitionsEnabled(bool v) { self()->stackedWidgetTransitionsEnabled_ = v; }
    static bool progressBarAnimated() { return self()->progressBarAnimated_; }
    static void setProgressBarAnimated(bool v) { self()->progressBarAnimated_ = v; }
    static int progressBarBusyStepDuration() { return self()->progressBarBusyStepDuration_; }
    static void setProgressBarBusyStepDuration(int v) { self()->progressBarBusyStepDuration_ = v; }
    static int scrollBarAddLineButtons() { return self()->scrollBarAddLineButtons_; }
    static void setScrollBarAddLineButtons(int v) { self()->scrollBarAddLineButtons_ = v; }
    static int scrollBarSubLineButtons() { return self()->scrollBarSubLineButtons_; }
    static void setScrollBarSubLineButtons(int v) { self()->scrollBarSubLineButtons_ = v; }
    static int mnemonicsMode() { return self()->mnemonicsMode_; }
    static void setMnemonicsMode(int v) { self()->mnemonicsMode_ = v; }
    static bool toolBarDrawItemSeparator() { return self()->toolBarDrawItemSeparator_; }
    static void setToolBarDrawItemSeparator(bool v) { self()->toolBarDrawItemSeparator_ = v; }
    static bool viewDrawFocusIndicator() { return self()->viewDrawFocusIndicator_; }
    static void setViewDrawFocusIndicator(bool v) { self()->viewDrawFocusIndicator_ = v; }
    static bool sliderDrawTickMarks() { return self()->sliderDrawTickMarks_; }
    static void setSliderDrawTickMarks(bool v) { self()->sliderDrawTickMarks_ = v; }
    static bool viewDrawTreeBranchLines() { return self()->viewDrawTreeBranchLines_; }
    static void setViewDrawTreeBranchLines(bool v) { self()->viewDrawTreeBranchLines_ = v; }
    static bool viewInvertSortIndicator() { return self()->viewInvertSortIndicator_; }
    static void setViewInvertSortIndicator(bool v) { self()->viewInvertSortIndicator_ = v; }
    static bool tabBarDrawCenteredTabs() { return self()->tabBarDrawCenteredTabs_; }
    static void setTabBarDrawCenteredTabs(bool v) { self()->tabBarDrawCenteredTabs_ = v; }
    static bool dockWidgetDrawFrame() { return self()->dockWidgetDrawFrame_; }
    static void setDockWidgetDrawFrame(bool v) { self()->dockWidgetDrawFrame_ = v; }
    static bool sidePanelDrawFrame() { return self()->sidePanelDrawFrame_; }
    static void setSidePanelDrawFrame(bool v) { self()->sidePanelDrawFrame_ = v; }
    static bool menuItemDrawStrongFocus() { return self()->menuItemDrawStrongFocus_; }
    static void setMenuItemDrawStrongFocus(bool v) { self()->menuItemDrawStrongFocus_ = v; }
    static int windowDragMode() { return self()->windowDragMode_; }
    static void setWindowDragMode(int v) { self()->windowDragMode_ = v; }
    static QStringList windowDragWhiteList() { return self()->windowDragWhiteList_; }
    static void setWindowDragWhiteList(const QStringList& v) { self()->windowDragWhiteList_ = v; }
    static QStringList windowDragBlackList() { return self()->windowDragBlackList_; }
    static void setWindowDragBlackList(const QStringList& v) { self()->windowDragBlackList_ = v; }
    static bool splitterProxyEnabled() { return self()->splitterProxyEnabled_; }
    static void setSplitterProxyEnabled(bool v) { self()->splitterProxyEnabled_ = v; }
    static int splitterProxyWidth() { return self()->splitterProxyWidth_; }
    static void setSplitterProxyWidth(int v) { self()->splitterProxyWidth_ = v; }
    static bool widgetExplorerEnabled() { return self()->widgetExplorerEnabled_; }
    static void setWidgetExplorerEnabled(bool v) { self()->widgetExplorerEnabled_ = v; }
    static bool drawWidgetRects() { return self()->drawWidgetRects_; }
    static void setDrawWidgetRects(bool v) { self()->drawWidgetRects_ = v; }
    static int menuOpacity() { return self()->menuOpacity_; }
    static void setMenuOpacity(int v) { self()->menuOpacity_ = v; }

private:
    StyleConfigData();
    KSharedConfig::Ptr config_;
    int shadowStrength_ = {};
    int shadowSize_ = {};
    QColor shadowColor_ = {};
    bool outlineCloseButton_ = {};
    bool animationsEnabled_ = {};
    int animationSteps_ = {};
    int animationsDuration_ = {};
    bool stackedWidgetTransitionsEnabled_ = {};
    bool progressBarAnimated_ = {};
    int progressBarBusyStepDuration_ = {};
    int scrollBarAddLineButtons_ = {};
    int scrollBarSubLineButtons_ = {};
    int mnemonicsMode_ = {};
    bool toolBarDrawItemSeparator_ = {};
    bool viewDrawFocusIndicator_ = {};
    bool sliderDrawTickMarks_ = {};
    bool viewDrawTreeBranchLines_ = {};
    bool viewInvertSortIndicator_ = {};
    bool tabBarDrawCenteredTabs_ = {};
    bool dockWidgetDrawFrame_ = {};
    bool sidePanelDrawFrame_ = {};
    bool menuItemDrawStrongFocus_ = {};
    int windowDragMode_ = {};
    QStringList windowDragWhiteList_ = {};
    QStringList windowDragBlackList_ = {};
    bool splitterProxyEnabled_ = {};
    int splitterProxyWidth_ = {};
    bool widgetExplorerEnabled_ = {};
    bool drawWidgetRects_ = {};
    int menuOpacity_ = {};
};

}
