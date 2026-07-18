#include "SettingsPanelWidget.h"
#include "EditLLMProfileDialog.h"
#include "StyleUtils.h"
#include "ConfigManager.h"
#include "CharacterPackManager.h"
#include "CharacterPack.h"
#include "MemoryManager.h"
#include "PersonaEngine.h"
#include "llm/LLMProfile.h"

#ifdef SEELIE_TTS_ENABLED
#include "tts/TTSProviderRegistry.h"
#endif

#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QStackedWidget>
#include <QWindow>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScreen>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QFont>
#include <QPixmap>
#include <QShowEvent>
#include <QStyle>
#include <QStyleOptionButton>

#include "PlatformWindow.h"
#include <QKeySequenceEdit>
#include <QTemporaryDir>
#include <QDir>
#include <QStandardPaths>
#include <QPolygon>
#include <QFile>
#include <QListView>
#include <QListWidget>
#include <QGroupBox>
#include <QToolButton>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QTransform>
#include <QElapsedTimer>

// All UI fonts in this panel are HarmonyOS Sans SC. The panel is translucent,
// so Windows can't apply ClearType subpixel AA — Qt falls back to grayscale.
// PreferAntialias keeps glyphs smoothed regardless of subpixel-positioning
// quirks; PreferNoHinting avoids stroke-snapping that mangles CJK glyphs at
// small point sizes.
static QFont harmonyFont(int pointSize, QFont::Weight weight = QFont::Normal)
{
    QFont f(QStringLiteral("HarmonyOS Sans SC"), pointSize, weight);
    f.setStyleStrategy(QFont::PreferAntialias);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

namespace {

// Static lookup table for TTS provider field labels. Replaces the former
// dynamic tr(qPrintable(...)) call which lupdate cannot statically scan.
static QString labelForField(const QString &field)
{
    if (field == QLatin1String("token"))   return QObject::tr("Token");
    if (field == QLatin1String("baseUrl")) return QObject::tr("BaseUrl");
    if (field == QLatin1String("model"))   return QObject::tr("Model");
    if (field == QLatin1String("groupId")) return QObject::tr("GroupId");
    if (field == QLatin1String("key"))     return QObject::tr("Key");
    if (field == QLatin1String("region"))  return QObject::tr("Region");
    if (field == QLatin1String("voice"))   return QObject::tr("Voice");
    // Fallback for unknown fields: capitalize first letter
    return field.left(1).toUpper() + field.mid(1);
}

// QSS can color the indicator box but cannot draw the tick glyph. Override
// paintEvent to overlay a checkmark on top of the styled box when checked.
class CheckMarkBox : public QCheckBox {
public:
    using QCheckBox::QCheckBox;
protected:
    void paintEvent(QPaintEvent *e) override {
        QCheckBox::paintEvent(e);
        if (!isChecked()) return;
        QStyleOptionButton opt;
        initStyleOption(&opt);
        const QRect r = style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt, this);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(Qt::white);
        pen.setWidthF(1.8);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        const qreal x = r.x();
        const qreal y = r.y();
        const qreal w = r.width();
        const qreal h = r.height();
        QPainterPath path;
        path.moveTo(x + w * 0.22, y + h * 0.52);
        path.lineTo(x + w * 0.42, y + h * 0.72);
        path.lineTo(x + w * 0.78, y + h * 0.32);
        p.drawPath(path);
    }
};
}

SettingsPanelWidget::SettingsPanelWidget(ConfigManager *config, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
    , m_memory(nullptr)
{
    setWindowFlags(
        Qt::FramelessWindowHint |
        Qt::WindowStaysOnTopHint |
        Qt::Tool
    );
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    // Same Win11 DWM workaround as MainWindow / TipWidget — without
    // WA_NoSystemBackground the system fills the panel's window with white
    // before paintEvent runs, and DWM then draws rounded corners + shadow
    // + Mica around it.
    setAttribute(Qt::WA_NoSystemBackground, true);
#ifdef Q_OS_MAC
    setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
#endif

    setFixedSize(PANEL_WIDTH + SHADOW_BLUR * 2, PANEL_HEIGHT + SHADOW_BLUR * 2);

    setupUi();

    // Read initial state from ConfigManager
    QString lang = m_config->language();
    int langIndex = (lang == "zh_CN") ? 1 : 0;
    m_langCombo->setCurrentIndex(langIndex);

    bool autoStart = m_config->autoStart();
    m_autoStartCheck->setChecked(autoStart);

    // Sync mode combo to current config value
    const int modeIndex = (m_config->displayMode() == ConfigManager::DisplayMode::Ecg) ? 1 : 0;
    m_modeCombo->setCurrentIndex(modeIndex);

    // Reflect initial pack-row visibility
    updatePackRowVisibility();
}

void SettingsPanelWidget::anchorTo(const QWidget *petWidget)
{
    if (petWidget) {
        positionRelativeTo(petWidget);
    }
}

void SettingsPanelWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    PlatformWindow::applyDwmFramelessAttributes(this);
    // Re-apply tab styles after the native window has been realized.
    // The setStyleSheet calls in setupUi() happen before the panel
    // has a real widget tree to polish; on the very first show some
    // platforms render the buttons with the un-styled compact size,
    // making them appear edge-to-edge until the user clicks one. This
    // re-poke is a no-op visually if styles were already correct, but
    // ensures both tabs render their padding+border on first paint.
    if (m_generalTabBtn && m_ttsTabBtn) {
        int currentTab = 0;
        if (m_llmTab && m_llmTab->isVisible()) currentTab = 3;
        else if (m_profileTab && m_profileTab->isVisible()) currentTab = 2;
        else if (m_ttsTab && m_ttsTab->isVisible()) currentTab = 1;
        onTabChanged(currentTab);
    }
}

void SettingsPanelWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF body(SHADOW_BLUR, SHADOW_BLUR, PANEL_WIDTH, PANEL_HEIGHT);
    const qreal r = CORNER_RADIUS;
    const qreal sk = SKEW_PX;

    // Build skewed panel path (matching tip bubble parallelogram)
    QPainterPath panelPath;
    panelPath.moveTo(body.left() + sk + r, body.top());
    panelPath.lineTo(body.right() + sk - r, body.top());
    panelPath.quadTo(body.right() + sk, body.top(), body.right() + sk, body.top() + r);
    panelPath.lineTo(body.right(), body.bottom() - r);
    panelPath.quadTo(body.right(), body.bottom(), body.right() - r, body.bottom());
    panelPath.lineTo(body.left() + r, body.bottom());
    panelPath.quadTo(body.left(), body.bottom(), body.left(), body.bottom() - r);
    panelPath.lineTo(body.left() + sk, body.top() + r);
    panelPath.quadTo(body.left() + sk, body.top(), body.left() + sk + r, body.top());
    panelPath.closeSubpath();

    // Bold shadow
    painter.save();
    painter.setOpacity(0.35);
    painter.setPen(Qt::NoPen);
    QPainterPath shadowPath = panelPath;
    shadowPath.translate(3, 4);
    painter.setBrush(Qt::black);
    painter.drawPath(shadowPath);
    painter.restore();

    // White fill + thick black border
    painter.setPen(QPen(Qt::black, BORDER_WIDTH, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    painter.setBrush(Qt::white);
    painter.drawPath(panelPath);

    // Red accent stripe at top
    painter.save();
    painter.setClipPath(panelPath);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xF3, 0x6F, 0x1A));
    painter.drawRect(QRectF(body.left(), body.top(), body.width() + sk, 4));
    painter.restore();
}

QGroupBox *SettingsPanelWidget::makeSectionGroup(const QString &title)
{
    // Section header style: bold orange title sitting above a thin grey
    // separator line. margin-top is just enough to fit the title — the
    // visible gap *between* groups comes from the parent layout's spacing,
    // not from oversized title margins.
    static const QString qss = QStringLiteral(R"(
        QGroupBox {
            background: transparent;
            border: none;
            border-top: 1px solid #B8B8B8;
            margin-top: 20px;
            padding: 0px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 0px;
            top: 2px;
            padding: 0 0 4px 0;
            color: #F36F1A;
            background: transparent;
            font-weight: bold;
        }
    )");
    auto *g = new QGroupBox(title, m_contentWidget);
    QFont titleFont = harmonyFont(11, QFont::Bold);
    titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    g->setFont(titleFont);
    g->setStyleSheet(qss);
    return g;
}

void SettingsPanelWidget::setupUi()
{
    // Create content widget that sits inside the panel area
    m_contentWidget = new QWidget(this);
    m_contentWidget->setGeometry(SHADOW_BLUR, SHADOW_BLUR, PANEL_WIDTH, PANEL_HEIGHT);
    m_contentWidget->setStyleSheet("background: transparent;");

    // Main vertical layout for content
    QVBoxLayout *mainLayout = new QVBoxLayout(m_contentWidget);
    mainLayout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    mainLayout->setSpacing(VERTICAL_SPACING);

    // Title row: "Settings" label + close button
    QHBoxLayout *titleRow = new QHBoxLayout();
    titleRow->setSpacing(4);

    m_titleLabel = new QLabel(tr("Settings"), m_contentWidget);
    m_titleLabel->setFont(harmonyFont(10, QFont::Bold));
    m_titleLabel->setStyleSheet("color: black; background: transparent;");
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_closeButton = new QPushButton(tr("×"), m_contentWidget);
    m_closeButton->setFont(harmonyFont(12, QFont::Bold));
    m_closeButton->setFixedSize(22, 22);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    m_closeButton->setStyleSheet(R"(
        QPushButton {
            background: transparent;
            border: none;
            border-radius: 3px;
            color: #888;
            padding: 0px;
        }
        QPushButton:hover {
            background: #F36F1A;
            color: white;
        }
    )");
    connect(m_closeButton, &QPushButton::clicked, this, &SettingsPanelWidget::onCloseClicked);

    titleRow->addWidget(m_titleLabel, 1);
    titleRow->addWidget(m_closeButton);

    // Separator line
    m_separator = new QFrame(m_contentWidget);
    m_separator->setFrameShape(QFrame::HLine);
    m_separator->setFrameShadow(QFrame::Plain);
    m_separator->setStyleSheet("border: none; border-top: 2px solid black; background: transparent;");
    m_separator->setFixedHeight(1);

    // Language row: label + combo
    m_langLabel = new QLabel(tr("Language"), m_contentWidget);
    m_langLabel->setFont(harmonyFont(10));
    m_langLabel->setStyleSheet("color: black; background: transparent;");
    m_langLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_langCombo = new QComboBox(m_contentWidget);
    // Force Qt-drawn popup instead of native macOS popup (native ignores stylesheets)
    auto *listView = new QListView(m_langCombo);
    listView->setFont(harmonyFont(10));
    m_langCombo->setView(listView);
    m_langCombo->addItem(tr("English"), "en");
    m_langCombo->addItem(tr("简体中文"), "zh_CN");
    m_langCombo->setFont(harmonyFont(10));
    m_langCombo->setFixedHeight(24);

    // Generate a small down-arrow pixmap (shared by both combos). Live in
    // AppLocalData rather than TempLocation so it: (a) doesn't litter the
    // system temp directory with a new orphan if Qt clears /tmp on reboot,
    // (b) is reused across launches without rewriting, and (c) lives next
    // to the app's other persistent state. L3.
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(cacheDir);
    QString arrowPath = cacheDir + "/combo_arrow.png";
    if (!QFile::exists(arrowPath)) {
        QPixmap arrow(8, 5);
        arrow.fill(Qt::transparent);
        QPainter p(&arrow);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(Qt::black);
        p.setPen(Qt::NoPen);
        QPolygon tri;
        tri << QPoint(0, 0) << QPoint(8, 0) << QPoint(4, 5);
        p.drawPolygon(tri);
        p.end();
        arrow.save(arrowPath);
    }

    // Shared combo style (language + mode)
    const QString comboStyleSheet = QStringLiteral(R"(
        QComboBox {
            background: white;
            border: 2px solid black;
            border-radius: 3px;
            padding: 2px 6px;
            color: #2C2C2E;
            min-width: 70px;
        }
        QComboBox::drop-down {
            border-left: 2px solid black;
            border-top-right-radius: 6px;
            border-bottom-right-radius: 6px;
            width: 18px;
            subcontrol-origin: padding;
            subcontrol-position: center right;
        }
        QComboBox::down-arrow {
            image: url(%1);
            width: 8px;
            height: 5px;
        }
        QComboBox QAbstractItemView {
            background: white;
            color: #2C2C2E;
            border: 2px solid black;
            border-radius: 4px;
            selection-background-color: #F36F1A;
            selection-color: white;
            outline: none;
        }
        QComboBox QAbstractItemView::item {
            color: #2C2C2E;
            padding: 3px 6px;
        }
        QComboBox QAbstractItemView::item:selected {
            background: #F36F1A;
            color: white;
        }
    )").arg(arrowPath);

    m_langCombo->setStyleSheet(comboStyleSheet);
    connect(m_langCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPanelWidget::onLanguageChanged);

    // Auto-start row: label + checkbox
    m_autoStartLabel = new QLabel(tr("Launch at Login"), m_contentWidget);
    m_autoStartLabel->setFont(harmonyFont(10));
    m_autoStartLabel->setStyleSheet("color: black; background: transparent;");
    m_autoStartLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_autoStartCheck = new CheckMarkBox(m_contentWidget);
    m_autoStartCheck->setFixedSize(16, 16);
    m_autoStartCheck->setStyleSheet(R"(
        QCheckBox::indicator {
            width: 12px;
            height: 12px;
            background: white;
            border: 2px solid black;
            border-radius: 3px;
        }
        QCheckBox::indicator:checked {
            background: #F36F1A;
            border: 1px solid #F36F1A;
        }
        QCheckBox::indicator:unchecked {
            background: white;
        }
    )");
    connect(m_autoStartCheck, &QCheckBox::toggled,
            this, &SettingsPanelWidget::onAutoStartToggled);

    // Mode row: label + combo  (lives in the Character group below)
    m_modeLabel = new QLabel(tr("Mode"), m_contentWidget);
    m_modeLabel->setFont(harmonyFont(10));
    m_modeLabel->setStyleSheet("color: black; background: transparent;");
    m_modeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_modeCombo = new QComboBox(m_contentWidget);
    auto *modeListView = new QListView(m_modeCombo);
    modeListView->setFont(harmonyFont(10));
    m_modeCombo->setView(modeListView);
    m_modeCombo->addItem(tr("Character", "display mode option"), "character");
    m_modeCombo->addItem(tr("ECG Monitor"), "ecg");
    m_modeCombo->setFont(harmonyFont(10));
    m_modeCombo->setFixedHeight(24);
    m_modeCombo->setStyleSheet(comboStyleSheet);
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPanelWidget::onModeChanged);

    // Port row: label + input
    m_portLabel = new QLabel(tr("Port"), m_contentWidget);
    m_portLabel->setFont(harmonyFont(10));
    m_portLabel->setStyleSheet("color: black; background: transparent;");
    m_portLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_portInput = new QLineEdit(m_contentWidget);
    m_portInput->setFont(harmonyFont(10));
    m_portInput->setText(QString::number(m_config->ipcPort()));
    m_portInput->setMaxLength(5);
    m_portInput->setFixedHeight(24);
    m_portInput->setStyleSheet(R"(
        QLineEdit {
            background: white;
            border: 2px solid black;
            border-radius: 3px;
            padding: 2px 6px;
            color: #2C2C2E;
        }
    )");
    connect(m_portInput, &QLineEdit::editingFinished,
            this, &SettingsPanelWidget::onPortEditingFinished);

    // Pack selection row: label + cascading button (mirrors the tray Pet menu)
    m_packLabel = new QLabel(tr("Model"), m_contentWidget);
    m_packLabel->setFont(harmonyFont(10));
    m_packLabel->setStyleSheet("color: black; background: transparent;");
    m_packLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_packButton = new QToolButton(m_contentWidget);
    m_packButton->setFont(harmonyFont(10));
    m_packButton->setFixedHeight(24);
    m_packButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_packButton->setPopupMode(QToolButton::InstantPopup);
    m_packButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_packButton->setText(tr("(no pack)"));
    m_packButton->setCursor(Qt::PointingHandCursor);
    m_packButton->setStyleSheet(QStringLiteral(R"(
        QToolButton {
            background: white;
            border: 2px solid black;
            border-radius: 3px;
            padding: 2px 22px 2px 6px;   /* right pad for arrow */
            color: #2C2C2E;
            min-width: 70px;
            text-align: left;
        }
        QToolButton::menu-indicator {
            image: url(%1);
            subcontrol-origin: padding;
            subcontrol-position: center right;
            right: 6px;
            width: 8px;
            height: 5px;
        }
        QToolButton:hover {
            background: #F36F1A;
            color: white;
        }
        QMenu {
            background: white;
            border: 2px solid black;
            border-radius: 4px;
            color: #2C2C2E;
            padding: 2px;
        }
        QMenu::item {
            padding: 4px 18px 4px 10px;
        }
        QMenu::item:selected {
            background: #F36F1A;
            color: white;
        }
        QMenu::item:checked {
            font-weight: bold;
        }
        QMenu::separator {
            height: 1px;
            background: black;
            margin: 2px 4px;
        }
    )").arg(arrowPath));

    // ----- General tab form -----
    // Settings are split across four group boxes (Application / Character /
    // Interaction / AI Features) using makeSectionGroup() for visually
    // distinct section headers.
    auto makeGroupGrid = [](QWidget *owner) -> QGridLayout* {
        auto *g = new QGridLayout(owner);
        g->setHorizontalSpacing(10);
        g->setVerticalSpacing(VERTICAL_SPACING);
        g->setContentsMargins(0, 8, 0, 0);
        g->setColumnStretch(1, 1);
        return g;
    };

    // --- Application group: language, login, shortcut, port ---
    m_appGroup = makeSectionGroup(tr("Application"));
    auto *appGrid = makeGroupGrid(m_appGroup);
    appGrid->addWidget(m_langLabel,       0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    appGrid->addWidget(m_langCombo,       0, 1);
    appGrid->addWidget(m_autoStartLabel,  1, 0, Qt::AlignLeft | Qt::AlignVCenter);
    appGrid->addWidget(m_autoStartCheck,  1, 1, Qt::AlignLeft | Qt::AlignVCenter);

    // --- Character group: display mode + pack picker ---
    m_characterGroup = makeSectionGroup(tr("Character", "settings section title"));
    auto *charGrid = makeGroupGrid(m_characterGroup);
    charGrid->addWidget(m_modeLabel,   0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    charGrid->addWidget(m_modeCombo,   0, 1);
    charGrid->addWidget(m_packLabel,   1, 0, Qt::AlignLeft | Qt::AlignVCenter);
    charGrid->addWidget(m_packButton,  1, 1);

    // Global shortcut row (placed in the Application group below).
    m_shortcutLabel = new QLabel(tr("Shortcut"), m_contentWidget);
    m_shortcutLabel->setFont(harmonyFont(10));
    m_shortcutLabel->setStyleSheet("color: black; background: transparent;");
    m_shortcutLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_shortcutEdit = new QKeySequenceEdit(QKeySequence(m_config->globalShortcut()), m_contentWidget);
    m_shortcutEdit->setFont(harmonyFont(10));
    m_shortcutEdit->setFixedHeight(24);
    m_shortcutEdit->setStyleSheet(R"(
        QKeySequenceEdit {
            background: transparent;
            border: none;
            padding: 0px;
        }
    )");
    if (QLineEdit *le = m_shortcutEdit->findChild<QLineEdit*>()) {
        le->setFrame(false);
        le->setStyleSheet(R"(
            QLineEdit {
                background: white;
                border: 2px solid black;
                border-radius: 3px;
                padding: 2px 6px;
                color: #2C2C2E;
            }
        )");
    }
    m_shortcutEdit->setToolTip(tr("Global shortcut to show/hide the pet"));
    appGrid->addWidget(m_shortcutLabel, 2, 0, Qt::AlignLeft | Qt::AlignVCenter);
    appGrid->addWidget(m_shortcutEdit,  2, 1);
    appGrid->addWidget(m_portLabel,     3, 0, Qt::AlignLeft | Qt::AlignVCenter);
    appGrid->addWidget(m_portInput,     3, 1);
    connect(m_shortcutEdit, &QKeySequenceEdit::keySequenceChanged,
            this, &SettingsPanelWidget::onShortcutChanged);

    // --- Interaction group: gaming mode + event tips ---
    m_interactionGroup = makeSectionGroup(tr("Interaction"));
    auto *interactGrid = makeGroupGrid(m_interactionGroup);

    m_gamingModeLabel = new QLabel(tr("Gaming Mode"), m_contentWidget);
    m_gamingModeLabel->setFont(harmonyFont(10));
    m_gamingModeLabel->setStyleSheet("color: black; background: transparent;");
    m_gamingModeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_gamingModeCheck = new CheckMarkBox(m_contentWidget);
    m_gamingModeCheck->setFixedSize(16, 16);
    m_gamingModeCheck->setChecked(m_config->gamingModeEnabled());
    m_gamingModeCheck->setStyleSheet(m_autoStartCheck->styleSheet());
    connect(m_gamingModeCheck, &QCheckBox::toggled,
            this, &SettingsPanelWidget::onGamingModeToggled);
    connect(m_config, &ConfigManager::gamingModeEnabledChanged,
            this, [this](bool enabled) {
        QSignalBlocker blocker(m_gamingModeCheck);
        m_gamingModeCheck->setChecked(enabled);
    });

    interactGrid->addWidget(m_gamingModeLabel, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    interactGrid->addWidget(m_gamingModeCheck, 0, 1, Qt::AlignLeft | Qt::AlignVCenter);

    m_tipBubblesLabel = new QLabel(tr("Event Tips"), m_contentWidget);
    m_tipBubblesLabel->setFont(harmonyFont(10));
    m_tipBubblesLabel->setStyleSheet("color: black; background: transparent;");
    m_tipBubblesLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_tipBubblesCheck = new CheckMarkBox(m_contentWidget);
    m_tipBubblesCheck->setFixedSize(16, 16);
    m_tipBubblesCheck->setChecked(m_config->tipBubblesEnabled());
    m_tipBubblesCheck->setStyleSheet(m_autoStartCheck->styleSheet());
    connect(m_tipBubblesCheck, &QCheckBox::toggled,
            this, &SettingsPanelWidget::onTipBubblesToggled);
    connect(m_config, &ConfigManager::tipBubblesEnabledChanged,
            this, [this](bool enabled) {
        QSignalBlocker blocker(m_tipBubblesCheck);
        m_tipBubblesCheck->setChecked(enabled);
    });

    interactGrid->addWidget(m_tipBubblesLabel, 1, 0, Qt::AlignLeft | Qt::AlignVCenter);
    interactGrid->addWidget(m_tipBubblesCheck, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);

    m_touchReactionsLabel = new QLabel(tr("Touch Reactions"), m_contentWidget);
    m_touchReactionsLabel->setFont(harmonyFont(10));
    m_touchReactionsLabel->setStyleSheet("color: black; background: transparent;");
    m_touchReactionsLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_touchReactionsCheck = new CheckMarkBox(m_contentWidget);
    m_touchReactionsCheck->setFixedSize(16, 16);
    m_touchReactionsCheck->setChecked(m_config->touchReactionsEnabled());
    m_touchReactionsCheck->setStyleSheet(m_autoStartCheck->styleSheet());
    connect(m_touchReactionsCheck, &QCheckBox::toggled,
            this, &SettingsPanelWidget::onTouchReactionsToggled);
    connect(m_config, &ConfigManager::touchReactionsEnabledChanged,
            this, [this](bool enabled) {
        QSignalBlocker blocker(m_touchReactionsCheck);
        m_touchReactionsCheck->setChecked(enabled);
    });

    interactGrid->addWidget(m_touchReactionsLabel, 2, 0, Qt::AlignLeft | Qt::AlignVCenter);
    interactGrid->addWidget(m_touchReactionsCheck, 2, 1, Qt::AlignLeft | Qt::AlignVCenter);

    // --- AI Features group: TTS + persona toggles ---
    m_aiFeaturesGroup = makeSectionGroup(tr("AI Features"));
    auto *aiGrid = makeGroupGrid(m_aiFeaturesGroup);
    int aiRow = 0;

#ifdef SEELIE_TTS_ENABLED
    // Enable TTS — lives on the General tab beside the persona toggle, since
    // users think of it as a feature toggle (like the persona) rather than
    // provider config.
    m_ttsEnabledLabel = new QLabel(tr("Enable TTS"), m_contentWidget);
    m_ttsEnabledLabel->setFont(harmonyFont(10));
    m_ttsEnabledLabel->setStyleSheet("color: black; background: transparent;");
    m_ttsEnabledLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_ttsEnabledCheck = new CheckMarkBox(m_contentWidget);
    m_ttsEnabledCheck->setFixedSize(16, 16);
    m_ttsEnabledCheck->setChecked(m_config->ttsEnabled());
    m_ttsEnabledCheck->setStyleSheet(m_autoStartCheck->styleSheet());
    connect(m_ttsEnabledCheck, &QCheckBox::toggled,
            this, &SettingsPanelWidget::onTtsEnabledToggled);
    connect(m_config, &ConfigManager::ttsEnabledChanged,
            this, [this](bool enabled) {
        QSignalBlocker blocker(m_ttsEnabledCheck);
        m_ttsEnabledCheck->setChecked(enabled);
    });

    aiGrid->addWidget(m_ttsEnabledLabel, aiRow, 0, Qt::AlignLeft | Qt::AlignVCenter);
    aiGrid->addWidget(m_ttsEnabledCheck, aiRow, 1, Qt::AlignLeft | Qt::AlignVCenter);
    ++aiRow;
#endif

    // --- AI persona toggle (lives in General alongside TTS Enable) -------
    m_personaEnabledLabel = new QLabel(tr("Enable AI persona"), m_contentWidget);
    m_personaEnabledLabel->setFont(harmonyFont(10));
    m_personaEnabledLabel->setStyleSheet("color: black; background: transparent;");
    m_personaEnabledLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_personaEnabledCheck = new CheckMarkBox(m_contentWidget);
    m_personaEnabledCheck->setFixedSize(16, 16);
    m_personaEnabledCheck->setChecked(m_config->personaEnabled());
    m_personaEnabledCheck->setStyleSheet(m_autoStartCheck->styleSheet());
    connect(m_personaEnabledCheck, &QCheckBox::toggled,
            m_config, &ConfigManager::setPersonaEnabled);

    aiGrid->addWidget(m_personaEnabledLabel, aiRow, 0, Qt::AlignLeft | Qt::AlignVCenter);
    aiGrid->addWidget(m_personaEnabledCheck, aiRow, 1, Qt::AlignLeft | Qt::AlignVCenter);

    // Tab buttons (left side). The active-vs-inactive stylesheet is
    // applied by onTabChanged() at the end of setupUi, but the buttons
    // need *some* padded stylesheet at construction time so their
    // sizeHint includes the padding — otherwise the QVBoxLayout below
    // sizes them to bare text and the 8 px spacing disappears between
    // adjacent tightly-sized buttons. The placeholder here gets
    // overwritten by onTabChanged(0) two lines after the layout
    // settles, with no visible flicker.
    const QString tabBtnPlaceholderStyle = R"(
        QPushButton {
            background: white;
            border: 2px solid #888;
            border-radius: 3px;
            padding: 6px 8px;
            text-align: left;
        }
    )";

    m_generalTabBtn = new QPushButton(tr("General"), m_contentWidget);
    m_generalTabBtn->setFont(harmonyFont(10, QFont::Bold));
    m_generalTabBtn->setFixedWidth(70);
    m_generalTabBtn->setCursor(Qt::PointingHandCursor);
    m_generalTabBtn->setCheckable(true);
    m_generalTabBtn->setChecked(true);
    m_generalTabBtn->setStyleSheet(tabBtnPlaceholderStyle);

    m_ttsTabBtn = new QPushButton(tr("TTS"), m_contentWidget);
    m_ttsTabBtn->setFont(harmonyFont(10, QFont::Bold));
    m_ttsTabBtn->setFixedWidth(70);
    m_ttsTabBtn->setCursor(Qt::PointingHandCursor);
    m_ttsTabBtn->setCheckable(true);
    m_ttsTabBtn->setStyleSheet(tabBtnPlaceholderStyle);

    m_profileTabBtn = new QPushButton(tr("Profile"), m_contentWidget);
    m_profileTabBtn->setFont(harmonyFont(10, QFont::Bold));
    m_profileTabBtn->setFixedWidth(70);
    m_profileTabBtn->setCursor(Qt::PointingHandCursor);
    m_profileTabBtn->setCheckable(true);
    m_profileTabBtn->setStyleSheet(tabBtnPlaceholderStyle);

    m_llmTabBtn = new QPushButton(tr("AI"), m_contentWidget);
    m_llmTabBtn->setFont(harmonyFont(10, QFont::Bold));
    m_llmTabBtn->setFixedWidth(70);
    m_llmTabBtn->setCursor(Qt::PointingHandCursor);
    m_llmTabBtn->setCheckable(true);
    m_llmTabBtn->setStyleSheet(tabBtnPlaceholderStyle);

    QVBoxLayout *tabBtnLayout = new QVBoxLayout();
    tabBtnLayout->setSpacing(8);
    tabBtnLayout->addWidget(m_generalTabBtn);
    tabBtnLayout->addWidget(m_ttsTabBtn);
    tabBtnLayout->addWidget(m_profileTabBtn);
    tabBtnLayout->addWidget(m_llmTabBtn);
    tabBtnLayout->addStretch(1);

    connect(m_generalTabBtn, &QPushButton::clicked, this, [this]() { onTabChanged(0); });
    connect(m_ttsTabBtn, &QPushButton::clicked, this, [this]() { onTabChanged(1); });
    connect(m_profileTabBtn, &QPushButton::clicked, this, [this]() { onTabChanged(2); });
    connect(m_llmTabBtn, &QPushButton::clicked, this, [this]() { onTabChanged(3); });

    // General tab content
    m_generalTab = new QWidget(m_contentWidget);
    QVBoxLayout *generalLayout = new QVBoxLayout(m_generalTab);
    generalLayout->setContentsMargins(0, 0, 0, 0);
    // Generous gap between groups so each section reads as its own block.
    // QGroupBox margin-top only reserves space for the title — the *visual*
    // breathing room between groups comes from this spacing.
    generalLayout->setSpacing(18);
    generalLayout->addWidget(m_appGroup);
    generalLayout->addWidget(m_characterGroup);
    generalLayout->addWidget(m_interactionGroup);
    generalLayout->addWidget(m_aiFeaturesGroup);

    generalLayout->addStretch(1);

    // AI tab content
    m_ttsTab = new QWidget(m_contentWidget);
    m_ttsTab->setVisible(false);
#ifdef SEELIE_TTS_ENABLED
    QVBoxLayout *aiLayout = new QVBoxLayout(m_ttsTab);
    aiLayout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    aiLayout->setSpacing(VERTICAL_SPACING);
#else
    QVBoxLayout *aiLayout = new QVBoxLayout(m_ttsTab);
    aiLayout->setContentsMargins(0, 0, 0, 0);
    aiLayout->setSpacing(VERTICAL_SPACING);
#endif

#ifdef SEELIE_TTS_ENABLED
    // === AI tab content ===
    // (Enable TTS toggle lives on the General tab — see above.)
    setupTtsTabContents(aiLayout, comboStyleSheet);
#else
    QLabel *ttsDisabledLabel = new QLabel(tr("TTS not available"), m_ttsTab);
    ttsDisabledLabel->setFont(harmonyFont(10));
    ttsDisabledLabel->setStyleSheet("color: #888; background: transparent;");
    ttsDisabledLabel->setAlignment(Qt::AlignCenter);
    aiLayout->addWidget(ttsDisabledLabel);
    aiLayout->addStretch(1);
#endif

    // Profile tab content (created empty — populated when MemoryManager is wired)
    m_profileTab = new QWidget(m_contentWidget);
    m_profileTab->setVisible(false);

    // AI / LLM tab content
    m_llmTab = new QWidget(m_contentWidget);
    m_llmTab->setVisible(false);
    {
        auto *llmLayout = new QVBoxLayout(m_llmTab);
        llmLayout->setContentsMargins(0, 0, 0, 0);
        // Same inter-group rhythm as the General tab.
        llmLayout->setSpacing(18);

        // --- Profiles group ---
        m_llmProfilesGroup = makeSectionGroup(tr("Profiles"));
        auto *profilesGroup = m_llmProfilesGroup;
        auto *pgLayout = new QVBoxLayout(profilesGroup);
        pgLayout->setContentsMargins(0, 8, 0, 0);
        pgLayout->setSpacing(8);
        m_llmProfilesList = new QListWidget(profilesGroup);
        m_llmProfilesList->setFont(harmonyFont(9));
        m_llmProfilesList->setStyleSheet(QStringLiteral("background: white; border: 2px solid black; border-radius: 3px;"));
        pgLayout->addWidget(m_llmProfilesList);
        auto *pgBtnRow = new QHBoxLayout;
        pgBtnRow->setSpacing(12);
        pgBtnRow->setContentsMargins(0, 0, 0, 0);
        m_llmAddBtn    = new QPushButton(tr("Add"),    profilesGroup);
        m_llmEditBtn   = new QPushButton(tr("Edit"),   profilesGroup);
        m_llmDeleteBtn = new QPushButton(tr("Delete"), profilesGroup);
        m_llmTestBtn   = new QPushButton(tr("Test"),   profilesGroup);
        m_llmTestBtn->setToolTip(tr("Test connection — sends a 1-token request to the selected profile"));
        for (auto *btn : {m_llmAddBtn, m_llmEditBtn, m_llmDeleteBtn, m_llmTestBtn}) {
            btn->setFont(harmonyFont(9));
            btn->setCursor(Qt::PointingHandCursor);
            btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            btn->setStyleSheet(QStringLiteral(R"(
                QPushButton {
                    background: white;
                    border: 2px solid black;
                    border-radius: 3px;
                    color: #2C2C2E;
                    padding: 4px 6px;
                    min-width: 36px;
                }
                QPushButton:hover {
                    background: #F36F1A;
                    color: white;
                }
                QPushButton:pressed {
                    background: #C85A12;
                    color: white;
                }
            )"));
        }
        pgBtnRow->addWidget(m_llmAddBtn);
        pgBtnRow->addWidget(m_llmEditBtn);
        pgBtnRow->addWidget(m_llmDeleteBtn);
        pgBtnRow->addWidget(m_llmTestBtn);
        pgLayout->addLayout(pgBtnRow);
        llmLayout->addWidget(profilesGroup);

        // The default-profile picker is the right-click context menu on the
        // Profiles list above; the default profile is marked with a ✓ prefix.
        // m_personaEnabledCheck lives in the General tab next to "Enable TTS"
        // so the two global feature toggles are co-located.
        m_llmProfilesList->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_llmProfilesList, &QListWidget::customContextMenuRequested,
                this, &SettingsPanelWidget::onProfilesListContextMenu);

        // --- Privacy group ---
        m_llmPrivacyGroup = makeSectionGroup(tr("Privacy"));
        auto *privacyGroup = m_llmPrivacyGroup;
        auto *privLayout = new QVBoxLayout(privacyGroup);
        privLayout->setContentsMargins(0, 8, 0, 0);
        privLayout->setSpacing(8);
        m_shareMemoryCheck = new CheckMarkBox(tr("Share memory with AI"), privacyGroup);
        m_shareMemoryCheck->setToolTip(tr("Sends your name, relationship stats (bond, affection, sessions), and recent activity summaries to the AI provider with each on-demand event."));
        m_shareMemoryCheck->setStyleSheet(m_autoStartCheck->styleSheet());
        privLayout->addWidget(m_shareMemoryCheck);
        llmLayout->addWidget(privacyGroup);

        // --- Regenerate button (inline — no group box needed for a single action) ---
        m_regenPoolBtn = new QPushButton(tr("Regenerate pool"), m_llmTab);
        m_regenPoolBtn->setFont(harmonyFont(9));
        m_regenPoolBtn->setCursor(Qt::PointingHandCursor);
        m_regenPoolBtn->setToolTip(tr("Wipe cached LLM responses for the active pack so they will be regenerated."));
        m_regenPoolBtn->setStyleSheet(StyleUtils::personaButtonQss());
        llmLayout->addWidget(m_regenPoolBtn);

        // --- Status label ---
        m_llmLastErrorLabel = new QLabel(m_llmTab);
        m_llmLastErrorLabel->setFont(harmonyFont(9));
        m_llmLastErrorLabel->setStyleSheet("color: #888; background: transparent;");
        m_llmLastErrorLabel->setWordWrap(true);
        // Initial text comes from renderLlmStatus() (Default kind). Going
        // through the helper keeps the label in lock-step with whatever
        // language the app is currently displaying.
        renderLlmStatus();
        llmLayout->addWidget(m_llmLastErrorLabel);
        llmLayout->addStretch();

        // --- Signal connections ---
        connect(m_llmAddBtn,    &QPushButton::clicked, this, &SettingsPanelWidget::onAddProfileClicked);
        connect(m_llmEditBtn,   &QPushButton::clicked, this, &SettingsPanelWidget::onEditProfileClicked);
        connect(m_llmDeleteBtn, &QPushButton::clicked, this, &SettingsPanelWidget::onDeleteProfileClicked);
        connect(m_llmTestBtn,   &QPushButton::clicked, this, &SettingsPanelWidget::onTestConnectionClicked);
        connect(m_regenPoolBtn, &QPushButton::clicked, this, &SettingsPanelWidget::onRegenPoolClicked);

        // m_personaEnabledCheck::toggled is connected in the General tab section.
        connect(m_shareMemoryCheck, &QCheckBox::toggled,
                m_config, &ConfigManager::setShareMemoryWithAi);

        connect(m_config, &ConfigManager::llmProfilesChanged,
                this, &SettingsPanelWidget::refreshLlmProfilesUi);
        // Re-render the list when the default profile changes so the ✓
        // moves to the right row.
        connect(m_config, &ConfigManager::personaProfileChanged,
                this, [this](const QString &) { refreshLlmProfilesUi(); });
    }

    // Populate profile list now that all widgets exist and signals are wired
    refreshLlmProfilesUi();

    // Tab content stacked area
    QHBoxLayout *tabContentLayout = new QHBoxLayout();
    tabContentLayout->setSpacing(8);
    tabContentLayout->addLayout(tabBtnLayout);
    tabContentLayout->addWidget(m_generalTab, 1);
    tabContentLayout->addWidget(m_ttsTab, 1);
    tabContentLayout->addWidget(m_profileTab, 1);
    tabContentLayout->addWidget(m_llmTab, 1);

    mainLayout->addLayout(titleRow);
    mainLayout->addWidget(m_separator);
    mainLayout->addLayout(tabContentLayout, 1);

    // Initialize tab styling (General selected by default)
    onTabChanged(0);
}

void SettingsPanelWidget::updatePackRowVisibility()
{
    const bool isCharacter = (m_config->displayMode() == ConfigManager::DisplayMode::Character);
    m_packLabel->setVisible(isCharacter);
    m_packButton->setVisible(isCharacter);
}

void SettingsPanelWidget::positionRelativeTo(const QWidget *pet)
{
    if (!pet)
        return;

    QRect anchor = m_anchorRect.isValid() ? m_anchorRect : QRect(0, 0, pet->width(), pet->height());

    // Same workaround as TipWidget::positionRelativeTo: macOS Qt::Tool
    // frameless windows return stale coords from mapToGlobal()/pos() because
    // their NSWindow position isn't always synced to QWidget. Use the native
    // QWindow position when available.
    QPoint petGlobalPos;
    if (QWindow *w = pet->windowHandle()) {
        petGlobalPos = w->position();
    } else {
        petGlobalPos = pet->mapToGlobal(QPoint(0, 0));
    }
    int petCenterX = petGlobalPos.x() + anchor.x() + anchor.width() / 2;
    int petTop = petGlobalPos.y() + anchor.y();
    int petBottom = petTop + anchor.height();

    // Default position: above the pet
    int panelX = petCenterX - PANEL_WIDTH / 2;
    int panelY = petTop - PANEL_HEIGHT - 5; // 5px gap

    // Check if we need to flip below
    QScreen *screen = QGuiApplication::screenAt(QPoint(petCenterX, petTop));
    if (screen) {
        QRect screenRect = screen->availableGeometry();

        // If not enough room above, flip to below
        if (panelY < screenRect.top()) {
            panelY = petBottom + 5;
        }

        // Clamp to screen edges
        panelX = qBound(screenRect.left(), panelX, screenRect.right() - PANEL_WIDTH);
        panelY = qBound(screenRect.top(), panelY, screenRect.bottom() - PANEL_HEIGHT);
    }

    // Account for shadow margin
    move(panelX - SHADOW_BLUR, panelY - SHADOW_BLUR);
}

void SettingsPanelWidget::onCloseClicked()
{
    hideAnimated();
}

void SettingsPanelWidget::setPanelScale(qreal s)
{
    m_scale = s;
    // Scale the content widget via transform from center
    QTransform t;
    qreal cx = SHADOW_BLUR + PANEL_WIDTH / 2.0;
    qreal cy = SHADOW_BLUR + PANEL_HEIGHT / 2.0;
    t.translate(cx, cy);
    t.scale(s, s);
    t.translate(-cx, -cy);
    m_contentWidget->setGeometry(
        SHADOW_BLUR + static_cast<int>(cx * (1.0 - s)),
        SHADOW_BLUR + static_cast<int>(cy * (1.0 - s)),
        static_cast<int>(PANEL_WIDTH * s),
        static_cast<int>(PANEL_HEIGHT * s));
    update();
}

void SettingsPanelWidget::setPanelOpacity(qreal o)
{
    m_panelOpacity = o;
    setWindowOpacity(o);
}

void SettingsPanelWidget::showAnimated()
{
    m_scale = 0.9;
    m_panelOpacity = 0.0;
    setWindowOpacity(0.0);
    QWidget::show();
    raise();

    if (m_scaleAnim) {
        m_scaleAnim->stop();
        delete m_scaleAnim;
        m_scaleAnim = nullptr;
    }
    if (m_opacityAnim) {
        m_opacityAnim->stop();
        delete m_opacityAnim;
        m_opacityAnim = nullptr;
    }

    // Scale: 0.9 → 1.0 with overshoot
    m_scaleAnim = new QPropertyAnimation(this, "panelScale", this);
    m_scaleAnim->setDuration(300);
    m_scaleAnim->setStartValue(0.9);
    m_scaleAnim->setEndValue(1.0);
    m_scaleAnim->setEasingCurve(QEasingCurve::OutBack);
    m_scaleAnim->start();

    // Fade in
    m_opacityAnim = new QPropertyAnimation(this, "panelOpacity", this);
    m_opacityAnim->setDuration(250);
    m_opacityAnim->setStartValue(0.0);
    m_opacityAnim->setEndValue(1.0);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_opacityAnim->start();
}

void SettingsPanelWidget::hideAnimated()
{
    if (m_scaleAnim) {
        m_scaleAnim->stop();
        delete m_scaleAnim;
        m_scaleAnim = nullptr;
    }
    if (m_opacityAnim) {
        m_opacityAnim->stop();
        delete m_opacityAnim;
        m_opacityAnim = nullptr;
    }

    // Scale: 1.0 → 0.9
    m_scaleAnim = new QPropertyAnimation(this, "panelScale", this);
    m_scaleAnim->setDuration(200);
    m_scaleAnim->setStartValue(1.0);
    m_scaleAnim->setEndValue(0.9);
    m_scaleAnim->setEasingCurve(QEasingCurve::InCubic);
    m_scaleAnim->start();

    // Fade out → hide when done
    m_opacityAnim = new QPropertyAnimation(this, "panelOpacity", this);
    m_opacityAnim->setDuration(200);
    m_opacityAnim->setStartValue(m_panelOpacity);
    m_opacityAnim->setEndValue(0.0);
    m_opacityAnim->setEasingCurve(QEasingCurve::InCubic);
    connect(m_opacityAnim, &QPropertyAnimation::finished, this, [this]() {
        hide();
        emit panelHidden();
    });
    m_opacityAnim->start();
}

void SettingsPanelWidget::onLanguageChanged(int index)
{
    QString langCode = m_langCombo->itemData(index).toString();
    m_config->setLanguage(langCode);
    m_config->save();
}

void SettingsPanelWidget::onAutoStartToggled(bool checked)
{
    m_config->setAutoStart(checked);
    m_config->save();
}

void SettingsPanelWidget::onModeChanged(int index)
{
    const QString modeData = m_modeCombo->itemData(index).toString();
    const ConfigManager::DisplayMode mode = (modeData == QStringLiteral("ecg"))
                                            ? ConfigManager::DisplayMode::Ecg
                                            : ConfigManager::DisplayMode::Character;
    m_config->setDisplayMode(mode);
    updatePackRowVisibility();
}

void SettingsPanelWidget::onPortEditingFinished()
{
    const QString text = m_portInput->text().trimmed();
    bool ok;
    int port = text.toInt(&ok);
    if (ok && port >= 1024 && port <= 65535) {
        m_config->setIpcPort(static_cast<quint16>(port));
    } else {
        m_portInput->setText(QString::number(m_config->ipcPort()));
    }
}

void SettingsPanelWidget::onShortcutChanged(const QKeySequence &sequence)
{
    m_config->setGlobalShortcut(sequence.toString());
}

void SettingsPanelWidget::onGamingModeToggled(bool checked)
{
    m_config->setGamingModeEnabled(checked);
}

void SettingsPanelWidget::onTipBubblesToggled(bool checked)
{
    m_config->setTipBubblesEnabled(checked);
}

void SettingsPanelWidget::onTouchReactionsToggled(bool checked)
{
    m_config->setTouchReactionsEnabled(checked);
}

void SettingsPanelWidget::onTabChanged(int tabIndex)
{
    m_generalTab->setVisible(tabIndex == 0);
    m_ttsTab->setVisible(tabIndex == 1);
    m_profileTab->setVisible(tabIndex == 2);
    if (m_llmTab) m_llmTab->setVisible(tabIndex == 3);

    const QString activeStyle = R"(
        QPushButton {
            background: #F36F1A;
            color: white;
            border: 2px solid black;
            border-radius: 3px;
            padding: 6px 8px;
            font-weight: bold;
            text-align: left;
        }
    )";
    const QString inactiveStyle = R"(
        QPushButton {
            background: white;
            color: black;
            border: 2px solid #888;
            border-radius: 3px;
            padding: 6px 8px;
            text-align: left;
        }
        QPushButton:hover {
            background: #F36F1A;
            color: white;
        }
    )";

    m_generalTabBtn->setStyleSheet(tabIndex == 0 ? activeStyle : inactiveStyle);
    m_ttsTabBtn->setStyleSheet(tabIndex == 1 ? activeStyle : inactiveStyle);
    m_profileTabBtn->setStyleSheet(tabIndex == 2 ? activeStyle : inactiveStyle);
    if (m_llmTabBtn) m_llmTabBtn->setStyleSheet(tabIndex == 3 ? activeStyle : inactiveStyle);
}

#ifdef SEELIE_TTS_ENABLED
void SettingsPanelWidget::onTtsEnabledToggled(bool checked)
{
    m_config->setTtsEnabled(checked);
}

void SettingsPanelWidget::onTtsProviderChanged(int comboIndex)
{
    const QString stableId = m_ttsProviderCombo->itemData(comboIndex).toString();
    if (stableId.isEmpty()) return;
    m_config->setTtsActiveProvider(stableId);
    m_ttsProviderStack->setCurrentIndex(comboIndex);
}

void SettingsPanelWidget::onTtsProviderFieldEdited()
{
    QLineEdit *src = qobject_cast<QLineEdit*>(sender());
    if (!src) return;
    for (const TTSFieldEdit& f : m_ttsFieldEdits) {
        if (f.edit == src) {
            m_config->setTtsProviderField(f.providerStableId, f.fieldName,
                                          src->text());
            return;
        }
    }
}

void SettingsPanelWidget::showAuthFailedHint(const QString &providerStableId)
{
    for (const TTSFieldEdit& f : m_ttsFieldEdits) {
        if (f.providerStableId == providerStableId &&
            (f.fieldName == QLatin1String("token") ||
             f.fieldName == QLatin1String("key")))
        {
            f.edit->setStyleSheet("border: 2px solid #E53E3E;");
            f.edit->setToolTip(tr("Authentication failed — check this credential."));
        }
    }
}
#endif

void SettingsPanelWidget::setMemoryManager(MemoryManager *memory)
{
    m_memory = memory;
    if (m_memory) {
        setupProfileTab();
    }
}

void SettingsPanelWidget::setupProfileTab()
{
    if (!m_memory || !m_profileTab) return;

    auto *layout = new QVBoxLayout(m_profileTab);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(VERTICAL_SPACING);

    // Name field
    m_nameLabel = new QLabel(tr("Name"), m_profileTab);
    m_nameLabel->setFont(harmonyFont(10));
    m_nameLabel->setStyleSheet("color: black; background: transparent;");
    m_nameEdit  = new QLineEdit(m_profileTab);
    m_nameEdit->setFont(harmonyFont(10));
    m_nameEdit->setPlaceholderText(tr("Your name"));
    m_nameEdit->setText(m_memory->userName());
    m_nameEdit->setFixedHeight(24);
    m_nameEdit->setStyleSheet(m_portInput->styleSheet());

    // Bio field — free-form "about me" sent to the persona LLM when the user
    // opted into memory sharing. Plain-text widget (not QTextEdit) so users
    // can write markdown source without auto-rich-text mangling newlines.
    m_bioLabel = new QLabel(tr("About you"), m_profileTab);
    m_bioLabel->setFont(harmonyFont(10));
    m_bioLabel->setStyleSheet("color: black; background: transparent;");
    m_bioEdit = new QPlainTextEdit(m_profileTab);
    m_bioEdit->setFont(harmonyFont(10));
    m_bioEdit->setPlaceholderText(tr("A few sentences — role, working style, anything you'd want a coworker to know. Markdown is fine."));
    m_bioEdit->setPlainText(m_memory->userBio());
    m_bioEdit->setFixedHeight(180);
    // Dedicated stylesheet — the QLineEdit { ... } selector on m_portInput
    // doesn't apply to QPlainTextEdit, so we'd inherit no border at all if
    // we reused it. Match the same 2px black border / white / rounded look.
    m_bioEdit->setStyleSheet(R"(
        QPlainTextEdit {
            background: white;
            border: 2px solid black;
            border-radius: 3px;
            padding: 4px 6px;
            color: #2C2C2E;
        }
    )");

    // Character counter. Soft cap — we don't refuse over-cap input, just turn
    // the counter red so the user can see they're about to bloat every LLM
    // request. Hard truncation lives on save (below).
    static constexpr int kBioMaxChars = 4000;
    m_bioCounterLabel = new QLabel(m_profileTab);
    m_bioCounterLabel->setFont(harmonyFont(9));
    m_bioCounterLabel->setAlignment(Qt::AlignRight);
    m_bioCounterLabel->setStyleSheet("color: #6b6b6b; background: transparent;");
    auto updateBioCounter = [this]() {
        const int len = m_bioEdit->toPlainText().length();
        m_bioCounterLabel->setText(QStringLiteral("%1 / %2").arg(len).arg(kBioMaxChars));
        m_bioCounterLabel->setStyleSheet(len > kBioMaxChars
            ? "color: #c0392b; background: transparent;"
            : "color: #6b6b6b; background: transparent;");
    };
    updateBioCounter();
    connect(m_bioEdit, &QPlainTextEdit::textChanged, this, updateBioCounter);

    // Save button
    m_saveBtn = new QPushButton(tr("Save"), m_profileTab);
    m_saveBtn->setFont(harmonyFont(10, QFont::Bold));
    m_saveBtn->setFixedHeight(28);
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    m_saveBtn->setStyleSheet(QStringLiteral(R"(
        QPushButton {
            background: white;
            border: 2px solid black;
            border-radius: 3px;
            color: #2C2C2E;
            padding: 2px 12px;
        }
        QPushButton:hover {
            background: #F36F1A;
            color: white;
        }
        QPushButton:pressed {
            background: #C85A12;
            color: white;
        }
    )"));
    connect(m_saveBtn, &QPushButton::clicked, this, [this]() {
        m_memory->setUserName(m_nameEdit->text().trimmed());
        // Hard-truncate bio at the cap before persisting — the counter warns
        // users but doesn't block typing; this is where we actually enforce.
        // Trim only outer whitespace; preserve internal newlines so markdown
        // structure (lists, blank lines between paragraphs) reaches the LLM.
        QString bio = m_bioEdit->toPlainText().trimmed();
        if (bio.length() > 4000) bio = bio.left(4000);
        m_memory->setUserBio(bio);
    });

    layout->addWidget(m_nameLabel);
    layout->addWidget(m_nameEdit);
    layout->addWidget(m_bioLabel);
    layout->addWidget(m_bioEdit);
    layout->addWidget(m_bioCounterLabel);
    layout->addWidget(m_saveBtn);
    layout->addStretch(1);
}

void SettingsPanelWidget::setCharacterPackManager(CharacterPackManager *manager)
{
    // H8: Disconnect previous manager's signals before wiring the new one.
    if (m_packManager) {
        disconnect(m_packManager, nullptr, this, nullptr);
    }

    m_packManager = manager;
    if (m_packManager) {
        // Keep the button label in sync when the active pack changes via any
        // other path (system tray, hot reload, etc).
        connect(m_packManager, &CharacterPackManager::activePackChanged,
                this, [this]() { updatePackButtonLabel(); });
        connect(m_packManager, &CharacterPackManager::packListChanged,
                this, &SettingsPanelWidget::refreshPackList);
    }
    refreshPackList();
}

// Mirror SystemTray::kCategoryOrder so the two menu surfaces show the same
// grouping in the same order. Keeping this in lock-step with the tray.
static const struct {
    const char *id;
    const char *labelEn;
} kCategoryOrder[] = {
    { "originals",       QT_TRANSLATE_NOOP("PackCategories", "Standalone") },
    { "azur_lane",       QT_TRANSLATE_NOOP("PackCategories", "Azur Lane") },
    { "girls_frontline", QT_TRANSLATE_NOOP("PackCategories", "Girls' Frontline") },
    { "idol_dimension",  QT_TRANSLATE_NOOP("PackCategories", "Idol Dimension") },
    { "konosuba",        QT_TRANSLATE_NOOP("PackCategories", "Konosuba") },
    { "live2d_samples",  QT_TRANSLATE_NOOP("PackCategories", "Live2D Samples") },
};

void SettingsPanelWidget::refreshPackList()
{
    qDebug() << "[REFRESH] SettingsPanelWidget::refreshPackList called";
    if (!m_packButton) {
        qDebug() << "  no pack button";
        return;
    }

    if (QMenu *old = m_packButton->menu()) {
        m_packButton->setMenu(nullptr);
        old->deleteLater();
    }

    if (!m_packManager) {
        m_packButton->setText(tr("(no pack)"));
        qDebug() << "  no pack manager";
        return;
    }

    QMenu *menu = new QMenu(m_packButton);
    menu->setFont(m_packButton->font());

    const auto packs = m_packManager->availablePacks();
    const QString activeId = m_packManager->activePackId();
    const QString locale = m_packManager->activeLocale();

    qDebug() << "  available packs count:" << packs.size() << "activeId:" << activeId;

    // Group by category (matches SystemTray::refreshPackMenu).
    QMap<QString, QVector<CharacterPackManager::PackInfo>> grouped;
    for (const auto &pack : packs) {
        const QString cat = pack.category.isEmpty()
                                ? QStringLiteral("originals") : pack.category;
        grouped[cat].append(pack);
    }

    qDebug() << "  grouped categories:";
    for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
        qDebug() << "    category:" << it.key() << "count:" << it.value().size();
    }

    QActionGroup *group = new QActionGroup(menu);
    group->setExclusive(true);

    auto addToSubmenu = [&](QMenu *sub, const QVector<CharacterPackManager::PackInfo> &list) {
        for (const auto &pack : list) {
            qDebug() << "    adding action for pack id:" << pack.id << "name:" << pack.displayName(locale);
            QAction *action = sub->addAction(pack.displayName(locale));
            action->setCheckable(true);
            action->setChecked(pack.id == activeId);
            group->addAction(action);
            const QString packId = pack.id;
            connect(action, &QAction::triggered, this, [this, packId]() {
                qDebug() << "[MENU] Action triggered for packId:" << packId;
                if (!m_packManager) return;
                if (!m_packManager->switchPack(packId)) {
                    // QActionGroup already moved the radio to the failed
                    // pack; rebuild the menu so it reflects truth.
                    refreshPackList();
                }
            });
        }
    };

    QSet<QString> seen;
    for (const auto &c : kCategoryOrder) {
        const QString id = QString::fromLatin1(c.id);
        if (!grouped.contains(id)) continue;
        QMenu *sub = menu->addMenu(QCoreApplication::translate("PackCategories", c.labelEn));
        sub->setFont(m_packButton->font());
        addToSubmenu(sub, grouped[id]);
        seen.insert(id);
    }
    for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
        if (seen.contains(it.key())) continue;
        QMenu *sub = menu->addMenu(it.key());
        sub->setFont(m_packButton->font());
        addToSubmenu(sub, it.value());
    }

    m_packButton->setMenu(menu);
    updatePackButtonLabel();
    qDebug() << "[REFRESH] refreshPackList done, menu set";
}

void SettingsPanelWidget::updatePackButtonLabel()
{
    if (!m_packButton || !m_packManager) return;
    const QString activeId = m_packManager->activePackId();
    const QString locale = m_packManager->activeLocale();
    for (const auto &pack : m_packManager->availablePacks()) {
        if (pack.id == activeId) {
            m_packButton->setText(pack.displayName(locale));
            return;
        }
    }
    m_packButton->setText(tr("(no pack)"));
}

void SettingsPanelWidget::retranslateUi()
{
    m_titleLabel->setText(tr("Settings"));
    m_closeButton->setText(tr("×"));
    m_langLabel->setText(tr("Language"));
    m_langCombo->setItemText(0, tr("English"));
    m_langCombo->setItemText(1, tr("简体中文"));
    m_autoStartLabel->setText(tr("Launch at Login"));
    m_modeLabel->setText(tr("Mode"));
    m_modeCombo->setItemText(0, tr("Character", "display mode option"));
    m_modeCombo->setItemText(1, tr("ECG Monitor"));
    m_portLabel->setText(tr("Port"));
    if (m_shortcutLabel) m_shortcutLabel->setText(tr("Shortcut"));
    if (m_shortcutEdit) m_shortcutEdit->setToolTip(tr("Global shortcut to show/hide the pet"));
    if (m_gamingModeLabel) m_gamingModeLabel->setText(tr("Gaming Mode"));
    if (m_tipBubblesLabel) m_tipBubblesLabel->setText(tr("Event Tips"));
    if (m_touchReactionsLabel) m_touchReactionsLabel->setText(tr("Touch Reactions"));
    if (m_packLabel) m_packLabel->setText(tr("Model"));
    if (m_appGroup) m_appGroup->setTitle(tr("Application"));
    if (m_characterGroup) m_characterGroup->setTitle(tr("Character", "settings section title"));
    if (m_interactionGroup) m_interactionGroup->setTitle(tr("Interaction"));
    if (m_aiFeaturesGroup) m_aiFeaturesGroup->setTitle(tr("AI Features"));
    if (m_generalTabBtn) m_generalTabBtn->setText(tr("General"));
    if (m_profileTabBtn) m_profileTabBtn->setText(tr("Profile"));
    if (m_llmTabBtn) m_llmTabBtn->setText(tr("AI"));
#ifdef SEELIE_TTS_ENABLED
    if (m_ttsTabBtn) m_ttsTabBtn->setText(tr("TTS"));
    if (m_ttsEnabledLabel) m_ttsEnabledLabel->setText(tr("Enable TTS"));
    if (m_personaEnabledLabel) m_personaEnabledLabel->setText(tr("Enable AI persona"));
    if (m_ttsProviderLabel) m_ttsProviderLabel->setText(tr("Provider"));
    if (m_ttsTestButton) m_ttsTestButton->setText(tr("Test"));
    if (m_ttsClearCacheButton) {
        m_ttsClearCacheButton->setText(tr("Clear cache"));
        m_ttsClearCacheButton->setToolTip(tr("Delete cached audio so the next utterance is freshly synthesised."));
    }
    // Refresh provider-field labels and the voice placeholder. These are
    // built dynamically per-provider in setupUi() and otherwise wouldn't
    // follow a runtime language switch.
    for (const TTSFieldEdit &f : m_ttsFieldEdits) {
        if (f.rowLabel) f.rowLabel->setText(labelForField(f.fieldName));
        if (f.edit && f.fieldName == QLatin1String("voice"))
            f.edit->setPlaceholderText(tr("Enter voice ID"));
    }
#endif
    // Pack labels can switch between English/Chinese on locale change.
    // Profile tab labels
    if (m_nameLabel) m_nameLabel->setText(tr("Name"));
    if (m_nameEdit) m_nameEdit->setPlaceholderText(tr("Your name"));
    if (m_bioLabel) m_bioLabel->setText(tr("About you"));
    if (m_bioEdit) m_bioEdit->setPlaceholderText(tr("A few sentences — role, working style, anything you'd want a coworker to know. Markdown is fine."));
    if (m_saveBtn) m_saveBtn->setText(tr("Save"));

    // AI / LLM tab — all of these were previously frozen in whatever language
    // the app booted in because nothing re-issued tr() on a language switch.
    if (m_llmProfilesGroup) m_llmProfilesGroup->setTitle(tr("Profiles"));
    if (m_llmPrivacyGroup)  m_llmPrivacyGroup->setTitle(tr("Privacy"));
    if (m_llmAddBtn)    m_llmAddBtn->setText(tr("Add"));
    if (m_llmEditBtn)   m_llmEditBtn->setText(tr("Edit"));
    if (m_llmDeleteBtn) m_llmDeleteBtn->setText(tr("Delete"));
    if (m_llmTestBtn) {
        m_llmTestBtn->setText(tr("Test"));
        m_llmTestBtn->setToolTip(tr("Test connection — sends a 1-token request to the selected profile"));
    }
    if (m_shareMemoryCheck) {
        m_shareMemoryCheck->setText(tr("Share memory with AI"));
        m_shareMemoryCheck->setToolTip(tr("Sends your name, relationship stats (bond, affection, sessions), and recent activity summaries to the AI provider with each on-demand event."));
    }
    if (m_regenPoolBtn) {
        m_regenPoolBtn->setText(tr("Regenerate pool"));
        m_regenPoolBtn->setToolTip(tr("Wipe cached LLM responses for the active pack so they will be regenerated."));
    }
    // Re-render the LLM status label in the new language. Whatever state it
    // was in (Default / Testing / Ok / Fail) gets re-issued through tr().
    renderLlmStatus();

    // Pack labels can switch between English/Chinese on locale change.
    if (m_packManager) {
        refreshPackList();
    }
}

#ifdef SEELIE_TTS_ENABLED
void SettingsPanelWidget::setupTtsTabContents(QVBoxLayout *aiLayout,
                                               const QString &comboStyleSheet)
{
    m_ttsProviderLabel = new QLabel(tr("Provider"), m_ttsTab);
    m_ttsProviderLabel->setFont(harmonyFont(10));
    m_ttsProviderLabel->setStyleSheet("color: black; background: transparent;");
    m_ttsProviderCombo = new QComboBox(m_ttsTab);
    // Install a QListView with the harmony font so the dropdown popup matches
    // the General-tab combos. Without this, the popup falls back to the
    // platform native list view (system fonts, default styling).
    {
        auto *providerListView = new QListView(m_ttsProviderCombo);
        providerListView->setFont(harmonyFont(10));
        m_ttsProviderCombo->setView(providerListView);
    }
    m_ttsProviderCombo->setFont(harmonyFont(10));
    m_ttsProviderCombo->setFixedHeight(24);
    m_ttsProviderCombo->setStyleSheet(comboStyleSheet);
    {
        QHBoxLayout *row = new QHBoxLayout();
        row->addWidget(m_ttsProviderLabel);
        row->addWidget(m_ttsProviderCombo, 1);
        aiLayout->addLayout(row);
    }

    m_ttsProviderStack = new QStackedWidget(m_ttsTab);
    aiLayout->addWidget(m_ttsProviderStack, 1);

    // Build one page per descriptor.
    using namespace seelie::tts;
    int activeIndex = 0;
    int comboIndex = 0;
    for (const ProviderDescriptor& desc : TTSProviderRegistry::descriptors()) {
        m_ttsProviderCombo->addItem(desc.displayName, desc.stableId);
        if (desc.stableId == m_config->ttsActiveProvider())
            activeIndex = comboIndex;
        ++comboIndex;

        QWidget *page = new QWidget(m_ttsProviderStack);
        QFormLayout *form = new QFormLayout(page);
        form->setContentsMargins(0, 0, 0, 0);
        form->setSpacing(8);
        // Match the General tab's left-aligned labels. QFormLayout's default
        // on macOS is right-aligned, which lined up oddly against the
        // grid-laid Settings rows above.
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        // Keep the same horizontal spacing between label column and field
        // column the General tab's QGridLayout uses (10 px).
        form->setHorizontalSpacing(10);
        // Don't let the rows wrap onto two lines on narrow widths — the
        // panel is fixed-width anyway.
        form->setRowWrapPolicy(QFormLayout::DontWrapRows);
        // Stretch the field column so QLineEdits fill available width like
        // QGridLayout's column-1 stretch does.
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        // Render every required + optional field as a QLineEdit. Voice is
        // a plain free-text field — users paste the provider's voice ID
        // (e.g. "cixingnansheng" for StepFun, "nova" for OpenAI).
        QStringList fields = desc.requiredFields + desc.optionalFields;
        for (const QString& field : fields) {
            QLineEdit *edit = new QLineEdit(page);
            edit->setFont(harmonyFont(10));
            edit->setFixedHeight(24);
            edit->setStyleSheet(m_portInput->styleSheet());
            edit->setText(m_config->ttsProviderField(desc.stableId, field));
            if (field == QLatin1String("token") || field == QLatin1String("key"))
                edit->setEchoMode(QLineEdit::Password);
            if (field == QLatin1String("voice"))
                edit->setPlaceholderText(tr("Enter voice ID"));
            connect(edit, &QLineEdit::editingFinished,
                    this, &SettingsPanelWidget::onTtsProviderFieldEdited);
            // Build the label widget explicitly so retranslateUi() can refresh
            // it. QFormLayout::addRow(QString, ...) constructs an internal
            // QLabel we'd have no handle on. Match the styling used elsewhere
            // on the panel for visual consistency.
            QLabel *rowLabel = new QLabel(labelForField(field), page);
            rowLabel->setFont(harmonyFont(10));
            rowLabel->setStyleSheet("color: black; background: transparent;");
            m_ttsFieldEdits.append({desc.stableId, field, edit, rowLabel});
            form->addRow(rowLabel, edit);
        }
        m_ttsProviderStack->addWidget(page);
    }

    m_ttsProviderCombo->setCurrentIndex(activeIndex);
    m_ttsProviderStack->setCurrentIndex(activeIndex);
    connect(m_ttsProviderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPanelWidget::onTtsProviderChanged);

    // Action row: primary "Test" on the left fills available space, secondary
    // "Clear voice cache" on the right is compact. Sharing one row keeps the
    // panel tight and visually balanced — stacked full-width buttons looked
    // like an afterthought.
    m_ttsTestButton = new QPushButton(tr("Test"), m_ttsTab);
    m_ttsTestButton->setFont(harmonyFont(10, QFont::Bold));
    m_ttsTestButton->setFixedHeight(28);
    m_ttsTestButton->setCursor(Qt::PointingHandCursor);
    m_ttsTestButton->setStyleSheet(QStringLiteral(R"(
        QPushButton {
            background: white;
            border: 2px solid black;
            border-radius: 3px;
            color: #2C2C2E;
            padding: 2px 12px;
        }
        QPushButton:hover {
            background: #F36F1A;
            color: white;
        }
        QPushButton:pressed {
            background: #C85A12;
            color: white;
        }
    )"));
    connect(m_ttsTestButton, &QPushButton::clicked, this, [this]() {
        emit testTtsRequested(tr("Hello. This is a TTS test from Seelie."));
    });

    m_ttsClearCacheButton = new QPushButton(tr("Clear cache"), m_ttsTab);
    m_ttsClearCacheButton->setFont(harmonyFont(10));
    m_ttsClearCacheButton->setFixedHeight(28);
    m_ttsClearCacheButton->setCursor(Qt::PointingHandCursor);
    m_ttsClearCacheButton->setToolTip(tr("Delete cached audio so the next utterance is freshly synthesised."));
    m_ttsClearCacheButton->setStyleSheet(QStringLiteral(R"(
        QPushButton {
            background: transparent;
            border: 1px solid #888;
            border-radius: 3px;
            color: #555;
            padding: 2px 12px;
        }
        QPushButton:hover {
            border-color: #2C2C2E;
            color: #2C2C2E;
            background: #F5F5F5;
        }
        QPushButton:pressed {
            background: #E8E8E8;
        }
    )"));
    connect(m_ttsClearCacheButton, &QPushButton::clicked,
            this, &SettingsPanelWidget::clearVoiceCacheRequested);

    QHBoxLayout *actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(8);
    actionRow->addWidget(m_ttsTestButton, 1);
    actionRow->addWidget(m_ttsClearCacheButton, 1);
    aiLayout->addLayout(actionRow);
}
#endif

// ---------------------------------------------------------------------------
// LLM / AI tab slots
// ---------------------------------------------------------------------------

void SettingsPanelWidget::refreshLlmProfilesUi()
{
    if (!m_config) return;
    m_llmProfilesList->clear();
    const QString defaultProfile = m_config->personaProfile();
    for (const auto &p : m_config->llmProfiles()) {
        QString protoName;
        switch (p.protocol) {
        case LLMProfile::Protocol::OpenAIChat:        protoName = tr("OpenAI Chat"); break;
        case LLMProfile::Protocol::OpenAIResponses:   protoName = tr("OpenAI Responses"); break;
        case LLMProfile::Protocol::AnthropicMessages: protoName = tr("Anthropic"); break;
        }
        // ✓ prefix on the default profile, two spaces on the others so names
        // stay column-aligned. Right-click → "Set as default" moves the mark.
        const QString prefix = (p.name == defaultProfile)
                               ? QStringLiteral("✓ ")
                               : QStringLiteral("  ");
        auto *item = new QListWidgetItem(
            prefix + QStringLiteral("%1  ·  %2").arg(p.name, p.model));
        item->setData(Qt::UserRole, p.name);  // raw name, no prefix
        item->setToolTip(protoName + (p.baseUrl.isEmpty()
                                      ? QString()
                                      : QStringLiteral("\n") + p.baseUrl));
        m_llmProfilesList->addItem(item);
    }
    m_personaEnabledCheck->setChecked(m_config->personaEnabled());
    m_shareMemoryCheck->setChecked(m_config->shareMemoryWithAi());
}

void SettingsPanelWidget::onProfilesListContextMenu(const QPoint &pos)
{
    if (!m_config) return;
    auto *item = m_llmProfilesList->itemAt(pos);
    if (!item) return;
    const QString name = item->data(Qt::UserRole).toString();
    if (name.isEmpty()) return;

    QMenu menu(this);
    QAction *setDefault = menu.addAction(tr("Set as default"));
    setDefault->setEnabled(name != m_config->personaProfile());
    menu.addSeparator();
    QAction *editAction   = menu.addAction(tr("Edit..."));
    QAction *deleteAction = menu.addAction(tr("Delete"));
    QAction *testAction   = menu.addAction(tr("Test connection"));

    QAction *chosen = menu.exec(m_llmProfilesList->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == setDefault) {
        m_config->setPersonaProfile(name);
    } else if (chosen == editAction) {
        onEditProfileClicked();
    } else if (chosen == deleteAction) {
        onDeleteProfileClicked();
    } else if (chosen == testAction) {
        onTestConnectionClicked();
    }
}

void SettingsPanelWidget::onAddProfileClicked()
{
    if (!m_config) return;
    EditLLMProfileDialog dlg({}, this);
    if (dlg.exec() != QDialog::Accepted) return;
    auto profiles = m_config->llmProfiles();
    profiles.append(dlg.profile());
    m_config->setLLMProfiles(profiles);
    refreshLlmProfilesUi();
}

void SettingsPanelWidget::onEditProfileClicked()
{
    if (!m_config) return;
    const int row = m_llmProfilesList->currentRow();
    if (row < 0) return;
    auto profiles = m_config->llmProfiles();
    if (row >= profiles.size()) return;
    EditLLMProfileDialog dlg(profiles[row], this);
    if (dlg.exec() != QDialog::Accepted) return;
    profiles[row] = dlg.profile();
    m_config->setLLMProfiles(profiles);
    refreshLlmProfilesUi();
}

void SettingsPanelWidget::onTestConnectionClicked()
{
    if (!m_config) return;
    const int row = m_llmProfilesList->currentRow();
    if (row < 0) {
        setLlmStatus(LlmStatusKind::SelectProfile);
        return;
    }
    const auto profile = m_config->llmProfiles().value(row);

    if (m_testProvider.isNull()) m_testProvider.reset(new LLMProvider(this));
    m_testProvider->setProfile(profile);
    m_testProvider->setTimeoutMs(5000);

    auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    setLlmStatus(LlmStatusKind::Testing);
    m_testProvider->generate(
        QStringLiteral("Reply with the single word OK."),
        QStringLiteral("ping"),
        [this, elapsed](LLMResult r) {
            const qint64 ms = elapsed->elapsed();
            if (!m_llmLastErrorLabel) return;
            if (r.ok) {
                setLlmStatus(LlmStatusKind::Ok, static_cast<int>(ms));
            } else {
                setLlmStatus(LlmStatusKind::Fail, r.error);
            }
        });
}

void SettingsPanelWidget::setLlmStatus(LlmStatusKind kind, const QVariant &arg)
{
    m_llmStatusKind = kind;
    m_llmStatusArg = arg;
    renderLlmStatus();
}

void SettingsPanelWidget::renderLlmStatus()
{
    if (!m_llmLastErrorLabel) return;
    switch (m_llmStatusKind) {
        case LlmStatusKind::Default:
            m_llmLastErrorLabel->setText(tr("Last error: —"));
            break;
        case LlmStatusKind::SelectProfile:
            m_llmLastErrorLabel->setText(tr("Select a profile first"));
            break;
        case LlmStatusKind::Testing:
            m_llmLastErrorLabel->setText(tr("Testing..."));
            break;
        case LlmStatusKind::Ok:
            m_llmLastErrorLabel->setText(tr("✓ %1 ms").arg(m_llmStatusArg.toInt()));
            break;
        case LlmStatusKind::Fail:
            // Error strings come from the LLM provider and are typically
            // language-independent (HTTP codes, API messages) — interpolate
            // verbatim. The "✗ " prefix and any future static framing run
            // through tr().
            m_llmLastErrorLabel->setText(tr("✗ %1").arg(m_llmStatusArg.toString()));
            break;
    }
}

void SettingsPanelWidget::onDeleteProfileClicked()
{
    if (!m_config) return;
    auto *item = m_llmProfilesList->currentItem();
    if (!item) return;
    const QString name = item->data(Qt::UserRole).toString();
    if (name.isEmpty()) return;
    auto profiles = m_config->llmProfiles();
    profiles.erase(std::remove_if(profiles.begin(), profiles.end(),
        [&](const LLMProfile &p){ return p.name == name; }), profiles.end());
    m_config->setLLMProfiles(profiles);
    // If we deleted the default profile, clear the assignment so the user
    // has to explicitly mark another (no silent inheritance of a stale name).
    if (m_config->personaProfile() == name) {
        m_config->setPersonaProfile(QString());
    }
    refreshLlmProfilesUi();
}

void SettingsPanelWidget::onRegenPoolClicked()
{
    if (m_personaEngine) m_personaEngine->regenerateActivePackPool();
}


