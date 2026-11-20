#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;

namespace controld {

class ProfileEditorDialog : public QDialog {
    Q_OBJECT

  public:
    ProfileEditorDialog(const QString& name, const QString& resolverId, QWidget* parent = nullptr);

    QString profileName() const;
    QString resolverId() const;

  public slots:
    void accept() override;

  private:
    QLineEdit* m_name = nullptr;
    QLineEdit* m_resolverId = nullptr;
};

} // namespace controld
