#ifndef SEELIE_TTS_PROVIDERCONFIG_H
#define SEELIE_TTS_PROVIDERCONFIG_H

#include <QHash>
#include <QString>

namespace seelie::tts {

// Free-form per-provider settings. The keys understood by each adapter are
// listed in its corresponding ProviderDescriptor::requiredFields/optionalFields.
// Centralizing the bag means ConfigManager can read/write a provider's whole
// subtree without compile-time coupling to each adapter's field list.
//
// SECURITY NOTE (audit L8): API tokens and subscription keys are stored as
// plain QString in memory, which means they remain readable in core dumps and
// swap. For production use, consider integrating the OS keychain (macOS
// Keychain / Windows Credential Manager / libsecret on Linux) via
// QKeychain or a similar library, and only holding tokens in memory for the
// minimum duration needed for each request.
struct ProviderConfig {
    QHash<QString, QString> values;

    QString get(const QString& key, const QString& defaultValue = {}) const {
        auto it = values.find(key);
        return it == values.end() ? defaultValue : *it;
    }
    bool has(const QString& key) const { return values.contains(key); }
};

} // namespace seelie::tts

#endif
