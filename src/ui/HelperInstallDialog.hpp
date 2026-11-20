#pragma once

class QWidget;

namespace controld {

class ProfileController;

void runHelperInstall(QWidget* parent);
void runHelperUninstall(QWidget* parent, ProfileController& controller);

} // namespace controld
