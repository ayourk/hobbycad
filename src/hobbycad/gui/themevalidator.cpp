// =====================================================================
//  src/hobbycad/gui/themevalidator.cpp — QSS theme validation
// =====================================================================

#include "themevalidator.h"

#include <QRegularExpression>
#include <QMap>

namespace hobbycad {

// Parse a QSS string into selector → property map entries.
// This is a lightweight parser — it handles the common cases
// (selectors with { } blocks, property: value; pairs) but does
// not attempt to be a full CSS parser.

ThemeValidationResult validateTheme(const QString& qss)
{
    ThemeValidationResult result;

    // Strip C-style comments
    QString clean = qss;
    QRegularExpression commentRx(QStringLiteral("/\\*.*?\\*/"),
        QRegularExpression::DotMatchesEverythingOption);
    clean.replace(commentRx, QString());

    // Match selector blocks:  selector { ... }
    QRegularExpression blockRx(
        QStringLiteral("([^{}]+)\\{([^{}]+)\\}"));

    auto it = blockRx.globalMatch(clean);
    while (it.hasNext()) {
        auto match = it.next();
        QString selector = match.captured(1).trimmed();
        QString body = match.captured(2);

        // Extract property values from the block body
        // Match:  property-name : value ;
        QRegularExpression propRx(
            QStringLiteral("([\\w-]+)\\s*:\\s*([^;]+);"));

        QString bgColor;
        QString fgColor;

        auto propIt = propRx.globalMatch(body);
        while (propIt.hasNext()) {
            auto propMatch = propIt.next();
            QString prop = propMatch.captured(1).trimmed().toLower();
            QString val  = propMatch.captured(2).trimmed().toLower();

            if (prop == QLatin1String("background-color")) {
                bgColor = val;
            } else if (prop == QLatin1String("color")) {
                fgColor = val;
            }
        }

        // Check for collision
        if (!bgColor.isEmpty() && !fgColor.isEmpty() &&
            bgColor == fgColor) {
            result.valid = false;
            result.warnings.append(
                QStringLiteral("Selector \"%1\": background-color "
                    "and color are both \"%2\" — text would be "
                    "invisible.")
                    .arg(selector, bgColor));
        }
    }

    return result;
}

}  // namespace hobbycad

