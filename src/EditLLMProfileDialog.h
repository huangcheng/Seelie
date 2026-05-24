#ifndef EDIT_LLM_PROFILE_DIALOG_H
#define EDIT_LLM_PROFILE_DIALOG_H

#include "llm/LLMProfile.h"
#include <QDialog>

class QLineEdit;
class QComboBox;

class EditLLMProfileDialog : public QDialog
{
    Q_OBJECT
public:
    explicit EditLLMProfileDialog(const LLMProfile &initial, QWidget *parent = nullptr);
    LLMProfile profile() const;

private:
    QLineEdit *m_name;
    QComboBox *m_protocol;
    QLineEdit *m_baseUrl;
    QLineEdit *m_apiKey;
    QLineEdit *m_model;
};

#endif
