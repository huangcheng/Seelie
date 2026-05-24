#include "EditLLMProfileDialog.h"
#include <QLineEdit>
#include <QComboBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPushButton>

EditLLMProfileDialog::EditLLMProfileDialog(const LLMProfile &initial, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("LLM Profile"));
    auto *form = new QFormLayout(this);

    m_name = new QLineEdit(initial.name, this);
    m_protocol = new QComboBox(this);
    m_protocol->addItem(tr("OpenAI Chat"),         int(LLMProfile::Protocol::OpenAIChat));
    m_protocol->addItem(tr("OpenAI Responses"),    int(LLMProfile::Protocol::OpenAIResponses));
    m_protocol->addItem(tr("Anthropic Messages"),  int(LLMProfile::Protocol::AnthropicMessages));
    m_protocol->setCurrentIndex(int(initial.protocol));
    m_baseUrl = new QLineEdit(initial.baseUrl, this);
    m_apiKey = new QLineEdit(initial.apiKey, this);
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_model = new QLineEdit(initial.model, this);

    form->addRow(tr("Name:"),     m_name);
    form->addRow(tr("Protocol:"), m_protocol);
    form->addRow(tr("Base URL:"), m_baseUrl);
    form->addRow(tr("API key:"),  m_apiKey);
    form->addRow(tr("Model:"),    m_model);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

LLMProfile EditLLMProfileDialog::profile() const
{
    LLMProfile p;
    p.name = m_name->text().trimmed();
    p.protocol = static_cast<LLMProfile::Protocol>(m_protocol->currentData().toInt());
    p.baseUrl = m_baseUrl->text().trimmed();
    p.apiKey = m_apiKey->text();
    p.model = m_model->text().trimmed();
    return p;
}
