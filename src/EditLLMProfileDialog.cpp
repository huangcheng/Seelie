#include "EditLLMProfileDialog.h"
#include "StyleUtils.h"
#include <QLineEdit>
#include <QComboBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPushButton>

EditLLMProfileDialog::EditLLMProfileDialog(const LLMProfile &initial, QWidget *parent)
    : PersonaDialog(tr("LLM Profile"), 400, 380, parent)
{
    setStyleSheet(StyleUtils::personaDialogQss());
    auto *form = new QFormLayout(contentWidget());
    form->setSpacing(16);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_name = new QLineEdit(initial.name, contentWidget());
    m_name->setMinimumHeight(28);
    m_protocol = new QComboBox(contentWidget());
    m_protocol->setMinimumHeight(28);
    m_protocol->setStyleSheet(StyleUtils::personaComboQss());
    m_protocol->addItem(tr("OpenAI Chat"),         int(LLMProfile::Protocol::OpenAIChat));
    m_protocol->addItem(tr("OpenAI Responses"),    int(LLMProfile::Protocol::OpenAIResponses));
    m_protocol->addItem(tr("Anthropic Messages"),  int(LLMProfile::Protocol::AnthropicMessages));
    m_protocol->setCurrentIndex(int(initial.protocol));
    m_baseUrl = new QLineEdit(initial.baseUrl, contentWidget());
    m_baseUrl->setMinimumHeight(28);
    m_apiKey = new QLineEdit(initial.apiKey, contentWidget());
    m_apiKey->setMinimumHeight(28);
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_model = new QLineEdit(initial.model, contentWidget());
    m_model->setMinimumHeight(28);

    form->addRow(tr("Name:"),     m_name);
    form->addRow(tr("Protocol:"), m_protocol);
    form->addRow(tr("Base URL:"), m_baseUrl);
    form->addRow(tr("API key:"),  m_apiKey);
    form->addRow(tr("Model:"),    m_model);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                    contentWidget());
    for (auto *btn : bb->findChildren<QPushButton*>()) {
        btn->setStyleSheet(StyleUtils::personaButtonQss());
        btn->setMinimumHeight(32);
        btn->setMinimumWidth(80);
    }
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
