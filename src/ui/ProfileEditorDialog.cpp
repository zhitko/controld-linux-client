#include "ui/ProfileEditorDialog.hpp"

#include "core/Validation.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

namespace controld {

ProfileEditorDialog::ProfileEditorDialog(const QString& name, const QString& resolverId,
                                         QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("ControlD profile"));
    auto* nameEdit = new QLineEdit(name, this);
    auto* idEdit = new QLineEdit(resolverId, this);
    nameEdit->setMaxLength(80);
    idEdit->setMaxLength(64);
    idEdit->setPlaceholderText(QStringLiteral("Resolver ID from the ControlD dashboard"));
    m_name = nameEdit;
    m_resolverId = idEdit;

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Name"), nameEdit);
    form->addRow(QStringLiteral("Resolver ID"), idEdit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    resize(420, 160);
}

QString ProfileEditorDialog::profileName() const {
    return m_name->text();
}

QString ProfileEditorDialog::resolverId() const {
    return m_resolverId->text().trimmed();
}

void ProfileEditorDialog::accept() {
    const QString nameError = profileNameError(profileName());
    if (!nameError.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Profile"), nameError);
        return;
    }
    const QString idError = resolverIdError(resolverId());
    if (!idError.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Profile"), idError);
        return;
    }
    QDialog::accept();
}

} // namespace controld
