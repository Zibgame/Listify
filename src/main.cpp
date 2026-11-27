#include "listifyapp.h"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Listify"));
    QCoreApplication::setOrganizationName(QStringLiteral("TheKindleNook"));
    ListifyApp listify;
    if (!listify.initialize(app.arguments())) {
        QTextStream(stderr) << "Impossible d'initialiser Listify.\n";
        return 1;
    }
    return listify.run();
}
