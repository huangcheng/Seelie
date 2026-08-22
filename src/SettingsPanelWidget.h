#ifndef SETTINGSPANELWIDGET_H
#define SETTINGSPANELWIDGET_H

#include "llm/LLMProvider.h"
#include <QWidget>
#include <QString>
#include <QPropertyAnimation>
#include <QListWidget>
#include <QScopedPointer>
#include <QVariant>

class ConfigManager;
class CharacterPackManager;
class MemoryManager;
class PersonaEngine;

class QLabel;
class QPushButton;
class QFrame;
class QComboBox;
class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QToolButton;
class QAction;
class QKeySequenceEdit;
class QStackedWidget;
class QVBoxLayout;
class QGroupBox;

class SettingsPanelWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal panelScale READ panelScale WRITE setPanelScale)
    Q_PROPERTY(qreal panelOpacity READ panelOpacity WRITE setPanelOpacity)

public:
    explicit SettingsPanelWidget(ConfigManager *config, QWidget *parent = nullptr);

    // Position relative to the pet widget
    void anchorTo(const QWidget *petWidget);
    void setAnchorRect(const QRect &rect) { m_anchorRect = rect; }

    // Set sprite pack manager for pack selection
    void setCharacterPackManager(CharacterPackManager *manager);

    // Set memory manager for profile tab
    void setMemoryManager(MemoryManager *memory);

    // Set persona engine for the AI tab's Regenerate button
    void setPersonaEngine(PersonaEngine *engine) { m_personaEngine = engine; }

    // Retranslate UI when language changes at runtime
    void retranslateUi();

    // Animated show/hide
    void showAnimated();
    void hideAnimated();

    qreal panelScale() const { return m_scale; }
    void setPanelScale(qreal s);
    qreal panelOpacity() const { return m_panelOpacity; }
    void setPanelOpacity(qreal o);

signals:
    void panelHidden();
#ifdef SEELIE_TTS_ENABLED
    // Emitted when the user clicks "Test" in the TTS tab. MainWindow wires
    // this to TTSEngine::speak so the test bypasses any tip flow.
    void testTtsRequested(const QString &text);
    // Emitted when the user clicks "Clear voice cache" in the TTS tab.
    void clearVoiceCacheRequested();
#endif

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

public slots:
#ifdef SEELIE_TTS_ENABLED
    void showAuthFailedHint(const QString &providerStableId);
#endif

private slots:
    void onCloseClicked();
    void onTabChanged(int tabIndex);
    void onLanguageChanged(int index);
    void onAutoStartToggled(bool checked);
    void onModeChanged(int index);
    void onPortEditingFinished();
    void onShortcutChanged(const QKeySequence &sequence);
    void onGamingModeToggled(bool checked);
    void onTipBubblesToggled(bool checked);
    void onTouchReactionsToggled(bool checked);
    void onDesktopWanderingToggled(bool checked);

    void refreshLlmProfilesUi();
    void onAddProfileClicked();
    void onEditProfileClicked();
    void onDeleteProfileClicked();
    void onTestConnectionClicked();
    void onRegenPoolClicked();
    void onProfilesListContextMenu(const QPoint &pos);
#ifdef SEELIE_TTS_ENABLED
    void onTtsEnabledToggled(bool checked);
    void onTtsProviderChanged(int comboIndex);
    void onTtsProviderFieldEdited();        // shared slot for all field editors
#endif

private:
    // Tracks what the LLM-status label is *meant* to display, separate from
    // the rendered text. Language switches re-render via renderLlmStatus()
    // without each call site having to remember to re-translate.
    enum class LlmStatusKind { Default, SelectProfile, Testing, Ok, Fail };
    void setLlmStatus(LlmStatusKind kind, const QVariant &arg = {});
    void renderLlmStatus();

    void setupUi();
    void setupProfileTab();
    // Build a QGroupBox styled as a section header — bold orange title with a
    // thin separator underneath and uniform spacing above. Used in both the
    // General and AI tabs so all section headers look identical.
    QGroupBox *makeSectionGroup(const QString &title);
#ifdef SEELIE_TTS_ENABLED
    /// Builds the TTS tab content (Provider combo, per-provider field
    /// pages, Test + Clear voice cache action row). Extracted from
    /// setupUi() so the long sequence of widget construction lives
    /// next to its retranslate / event-handling slots rather than
    /// halfway down a 580-line setupUi. Audit H11.
    void setupTtsTabContents(QVBoxLayout *aiLayout, const QString &comboStyleSheet);
#endif
    void positionRelativeTo(const QWidget *pet);
    void refreshPackList();
    void updatePackButtonLabel();
    void updatePackRowVisibility();

    ConfigManager *m_config;
    CharacterPackManager *m_packManager = nullptr;
    MemoryManager *m_memory = nullptr;

    // UI elements
    QLabel *m_titleLabel = nullptr;
    QPushButton *m_closeButton = nullptr;
    QFrame *m_separator = nullptr;
    QLabel *m_langLabel = nullptr;
    QComboBox *m_langCombo = nullptr;
    QLabel *m_autoStartLabel = nullptr;
    QCheckBox *m_autoStartCheck = nullptr;
    QLabel *m_modeLabel = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QLabel *m_portLabel = nullptr;
    QLineEdit *m_portInput = nullptr;
    QLabel *m_packLabel = nullptr;
    QToolButton *m_packButton = nullptr;
    QLabel *m_shortcutLabel = nullptr;
    QKeySequenceEdit *m_shortcutEdit = nullptr;
    QLabel *m_gamingModeLabel = nullptr;
    QCheckBox *m_gamingModeCheck = nullptr;
    QLabel *m_tipBubblesLabel = nullptr;
    QCheckBox *m_tipBubblesCheck = nullptr;
    QLabel *m_touchReactionsLabel = nullptr;
    QCheckBox *m_touchReactionsCheck = nullptr;
    QLabel *m_desktopWanderingLabel = nullptr;
    QCheckBox *m_desktopWanderingCheck = nullptr;
    QLabel *m_sayingsLabel = nullptr;
    QComboBox *m_sayingsCombo = nullptr;
    QLabel *m_personaEnabledLabel = nullptr;

    // Tab buttons (left side)
    QPushButton *m_generalTabBtn = nullptr;
    QPushButton *m_ttsTabBtn = nullptr;
    QPushButton *m_profileTabBtn = nullptr;
    QPushButton *m_llmTabBtn = nullptr;

    // Tab content containers
    QWidget *m_generalTab = nullptr;
    QWidget *m_ttsTab = nullptr;
    QWidget *m_profileTab = nullptr;
    QWidget *m_llmTab = nullptr;

    // General tab group boxes (held for retranslateUi)
    QGroupBox *m_appGroup = nullptr;
    QGroupBox *m_characterGroup = nullptr;
    QGroupBox *m_interactionGroup = nullptr;
    QGroupBox *m_aiFeaturesGroup = nullptr;

    // LLM / AI tab widgets
    QGroupBox    *m_llmProfilesGroup = nullptr;
    QGroupBox    *m_llmPrivacyGroup = nullptr;
    QListWidget  *m_llmProfilesList = nullptr;
    QPushButton  *m_llmAddBtn = nullptr;
    QPushButton  *m_llmEditBtn = nullptr;
    QPushButton  *m_llmDeleteBtn = nullptr;
    QPushButton  *m_llmTestBtn = nullptr;
    QCheckBox    *m_personaEnabledCheck = nullptr;
    QCheckBox    *m_shareMemoryCheck = nullptr;
    QCheckBox    *m_llmIdleQuipsCheck = nullptr;
    QPushButton  *m_regenPoolBtn = nullptr;
    QLabel       *m_llmLastErrorLabel = nullptr;
    LlmStatusKind m_llmStatusKind = LlmStatusKind::Default;
    QVariant      m_llmStatusArg;
    PersonaEngine *m_personaEngine = nullptr;
    QScopedPointer<LLMProvider> m_testProvider;

    // Profile tab widgets (stored for retranslation)
    QLabel *m_nameLabel = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLabel *m_bioLabel = nullptr;
    QPlainTextEdit *m_bioEdit = nullptr;
    QLabel *m_bioCounterLabel = nullptr;
    QPushButton *m_saveBtn = nullptr;

#ifdef SEELIE_TTS_ENABLED
    QLabel       *m_ttsEnabledLabel = nullptr;
    QCheckBox    *m_ttsEnabledCheck = nullptr;
    QLabel       *m_ttsProviderLabel = nullptr;
    QComboBox    *m_ttsProviderCombo = nullptr;
    QStackedWidget *m_ttsProviderStack = nullptr;
    QPushButton  *m_ttsTestButton = nullptr;
    QPushButton  *m_ttsClearCacheButton = nullptr;

    // Each provider's page contains a QFormLayout of QLineEdits keyed by
    // field name (including "voice" — it's just another text field). We
    // track them here so onTtsProviderFieldEdited() can route the edit
    // back to the right provider/field pair, and so retranslateUi() can
    // refresh the row label when the user switches language at runtime.
    struct TTSFieldEdit {
        QString providerStableId;
        QString fieldName;
        QLineEdit *edit;
        QLabel *rowLabel;  // form-row label, owned by the QFormLayout
    };
    QList<TTSFieldEdit> m_ttsFieldEdits;
#endif

    // Layout container
    QWidget *m_contentWidget = nullptr;
    QRect m_anchorRect;  // rect within the anchored widget to anchor to (empty = full widget)

    // Animation
    qreal m_scale = 1.0;
    qreal m_panelOpacity = 1.0;
    QPropertyAnimation *m_scaleAnim = nullptr;
    QPropertyAnimation *m_opacityAnim = nullptr;

    // Styling constants
    static constexpr int PADDING = 14;
    static constexpr int VERTICAL_SPACING = 12;
    static constexpr int SHADOW_BLUR = 10;
    static constexpr int CORNER_RADIUS = 4;
    static constexpr int BORDER_WIDTH = 3;
    static constexpr int SKEW_PX = 4;
    static constexpr int PANEL_WIDTH = 420;
    static constexpr int PANEL_HEIGHT = 580;
};

#endif // SETTINGSPANELWIDGET_H
