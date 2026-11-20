#pragma once

#include <QString>
#include <QStringList>

namespace controld {

struct HelperOutcome {
    int code = 1;
    QString error;
};

HelperOutcome runChromePolicyHelper(const QStringList& args, const QStringList& files);

} // namespace controld
