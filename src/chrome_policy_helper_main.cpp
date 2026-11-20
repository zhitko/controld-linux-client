#include "core/ChromePolicy.hpp"
#include "core/ChromePolicyHelper.hpp"

#include <QCoreApplication>

#include <cstdio>

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const controld::HelperOutcome outcome = controld::runChromePolicyHelper(
        application.arguments().mid(1), controld::ChromePolicy::managedPolicyPaths());
    if (!outcome.error.isEmpty()) {
        std::fprintf(stderr, "%s\n", qUtf8Printable(outcome.error));
    }
    return outcome.code;
}
