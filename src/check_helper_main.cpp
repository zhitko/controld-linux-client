#include "core/HelperInstall.hpp"

#include <QCoreApplication>
#include <QString>

#include <cstdio>

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    if (argc != 3) {
        std::fprintf(stderr, "Usage: controld-check-helper <helper> <appdir>\n");
        return 2;
    }
    const controld::LibraryPlan plan = controld::librariesForHelper(
        QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]));
    if (!plan.ok) {
        std::fprintf(stderr, "%s\n", qPrintable(plan.error));
        return 1;
    }
    for (const controld::BundledLibrary& library : plan.libraries) {
        std::fprintf(stdout, "%s\n", qPrintable(library.soname));
    }
    return 0;
}
