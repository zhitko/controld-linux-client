#pragma once

#include "core/IChromePolicyWriter.hpp"

#include <QString>

namespace controld {

class PkexecChromePolicyWriter : public IChromePolicyWriter {
  public:
    explicit PkexecChromePolicyWriter(QString helperPath);

    CallResult apply(const QString& resolverId) override;
    CallResult clear() override;

  private:
    CallResult run(const QStringList& arguments) const;
    CallResult validateHelper() const;

    QString m_helperPath;
};

QString defaultChromeHelperPath();

} // namespace controld
