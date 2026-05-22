#include "PackDropHandler.h"

#include "CharacterPackLoader.h"
#include "CharacterPackManager.h"
#include "TipWidget.h"
#include "TipsCatalog.h"

#include <QDebug>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QStandardPaths>
#include <QUrl>

namespace PackDropHandler {

bool isValidCodexPet(const QString &filePath)
{
    return CharacterPackLoader::isValidCodexPet(filePath);
}

void handleDragEnter(QDragEnterEvent *event)
{
    qDebug() << "PackDropHandler: dragEnterEvent";
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    for (const QUrl &url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (path.endsWith(".spk", Qt::CaseInsensitive)) {
            event->acceptProposedAction();
            return;
        }
        if ((path.endsWith(".codex-pet", Qt::CaseInsensitive) ||
             path.endsWith(".codex-pet.zip", Qt::CaseInsensitive)) &&
            isValidCodexPet(path)) {
            event->acceptProposedAction();
            return;
        }
    }
    qDebug() << "PackDropHandler: dragEnterEvent ignored";
    event->ignore();
}

namespace {

void handleSpkDrop(const QString &filePath,
                   CharacterPackManager *packManager,
                   TipWidget *tipWidget)
{
    const bool installed = packManager->installPack(filePath);
    const QString msgId = installed
                              ? QStringLiteral("pack.installed")
                              : QStringLiteral("pack.install_failed");
    const auto t = TipsCatalog::instance().message(msgId);
    tipWidget->showBubble(t.title, t.body, TipWidget::TipBubble, "", true);
    qDebug() << "PackDropHandler: .spk install" << (installed ? "succeeded" : "failed");
}

void handleCodexPetDrop(const QString &filePath,
                        CharacterPackManager *packManager,
                        TipWidget *tipWidget)
{
    QFileInfo fi(filePath);
    QString userPacksDir = packManager->userDir();
    if (userPacksDir.isEmpty()) {
        userPacksDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                       + "/packs";
        qWarning() << "PackDropHandler: userDir empty, falling back to" << userPacksDir;
    }
    QDir().mkpath(userPacksDir);

    // fi.fileName() is attacker-controllable via a crafted file:// URL
    // (e.g. "../../Library/LaunchAgents/evil.plist") or a filename that
    // crosses a path separator. Reduce to the base name and reject any
    // residual separator so the destination always stays inside
    // userPacksDir. H13.
    const QString safeName = QFileInfo(fi.fileName()).fileName();
    if (safeName.isEmpty() ||
        safeName.contains('/') || safeName.contains('\\') ||
        safeName == QStringLiteral(".") ||
        safeName == QStringLiteral("..")) {
        qWarning() << "PackDropHandler: rejecting drop with unsafe filename:" << fi.fileName();
        return;
    }
    const QString destFile = userPacksDir + "/" + safeName;

    if (QFile::exists(destFile)) {
        QFile::remove(destFile);
    }
    const bool ok = QFile::copy(filePath, destFile);
    qDebug() << "PackDropHandler: copied .codex-pet to" << destFile << "=" << ok;

    if (ok) {
        const QString builtInDir = packManager->builtInDir();
        const QString currentPackId = packManager->activePackId();
        packManager->initialize(builtInDir, userPacksDir, currentPackId);
        const auto t = TipsCatalog::instance().message(QStringLiteral("pack.installed"));
        tipWidget->showBubble(t.title, t.body, TipWidget::TipBubble, "", true);
    } else {
        const auto t = TipsCatalog::instance().message(QStringLiteral("pack.install_failed"));
        tipWidget->showBubble(t.title, t.body, TipWidget::TipBubble, "", true);
    }
}

} // namespace

void handleDrop(QDropEvent *event,
                CharacterPackManager *packManager,
                TipWidget *tipWidget)
{
    if (!packManager || !tipWidget) {
        qWarning() << "PackDropHandler: dropEvent ignored — missing manager or tip widget";
        event->ignore();
        return;
    }

    const QList<QUrl> urls = event->mimeData()->urls();
    qDebug() << "PackDropHandler: dropEvent with" << urls.size() << "URLs";

    bool handled = false;
    for (const QUrl &url : urls) {
        const QString filePath = url.toLocalFile();
        qDebug() << "PackDropHandler: dropped file:" << filePath;

        if (filePath.endsWith(".spk", Qt::CaseInsensitive)) {
            handleSpkDrop(filePath, packManager, tipWidget);
            handled = true;
        } else if ((filePath.endsWith(".codex-pet", Qt::CaseInsensitive) ||
                    filePath.endsWith(".codex-pet.zip", Qt::CaseInsensitive)) &&
                   isValidCodexPet(filePath)) {
            handleCodexPetDrop(filePath, packManager, tipWidget);
            handled = true;
        } else {
            qDebug() << "PackDropHandler: dropped file ignored:" << filePath;
        }
    }

    if (handled) event->acceptProposedAction();
    else event->ignore();
}

} // namespace PackDropHandler
