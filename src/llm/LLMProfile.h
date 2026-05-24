#ifndef LLM_PROFILE_H
#define LLM_PROFILE_H

#include <QString>

/**
 * @brief One user-configured LLM endpoint. Persisted via ConfigManager.
 *
 * Three protocols are supported, mapping to the three HTTP shapes the
 * AI Persona Layer can speak. The same struct represents any of them —
 * baseUrl + model + apiKey are interpreted per protocol.
 */
struct LLMProfile {
    enum class Protocol {
        OpenAIChat = 0,
        OpenAIResponses = 1,
        AnthropicMessages = 2,
    };

    QString name;
    Protocol protocol = Protocol::OpenAIChat;
    QString baseUrl;
    QString apiKey;
    QString model;
};

#endif // LLM_PROFILE_H
