#include "listifycore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

QString listingIdFromEtsyUrl(const QString &url)
{
    const QRegularExpression expression(QStringLiteral("/listing/(\\d+)(?:/|$|\\?)"),
                                        QRegularExpression::CaseInsensitiveOption);
    return expression.match(url).captured(1);
}

bool isExcludedShop(const QString &shopName)
{
    return shopName.trimmed().compare(QStringLiteral("TheKindleNook"), Qt::CaseInsensitive) == 0;
}

static QString outputTextFromResponse(const QJsonObject &root)
{
    QString text;
    for (const auto &itemValue : root.value(QStringLiteral("output")).toArray()) {
        const auto item = itemValue.toObject();
        if (item.value(QStringLiteral("type")).toString() != QStringLiteral("message"))
            continue;
        for (const auto &partValue : item.value(QStringLiteral("content")).toArray()) {
            const auto part = partValue.toObject();
            if (part.value(QStringLiteral("type")).toString() == QStringLiteral("output_text"))
                text += part.value(QStringLiteral("text")).toString();
        }
    }
    return text.trimmed();
}

bool parseSeoResponse(const QByteArray &responseData, SeoResult *result, QString *error)
{
    QJsonParseError parseError;
    const auto rootDocument = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !rootDocument.isObject()) {
        if (error) *error = QStringLiteral("Réponse OpenAI invalide : %1").arg(parseError.errorString());
        return false;
    }
    const QString outputText = outputTextFromResponse(rootDocument.object());
    const auto document = QJsonDocument::fromJson(outputText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Sortie SEO JSON invalide : %1").arg(parseError.errorString());
        return false;
    }
    const auto object = document.object();
    SeoResult parsed;
    parsed.title = object.value(QStringLiteral("title")).toString().trimmed();
    parsed.description = object.value(QStringLiteral("description")).toString().trimmed();
    parsed.imageFilename = normalizeImageFilename(object.value(QStringLiteral("image_filename")).toString());
    parsed.altText = object.value(QStringLiteral("alt_text")).toString().trimmed();
    for (const auto &tag : object.value(QStringLiteral("tags")).toArray())
        parsed.tags << tag.toString().trimmed();
    if (!validateSeoResult(parsed, error)) return false;
    if (result) *result = parsed;
    return true;
}

QString normalizeImageFilename(const QString &filename)
{
    QString normalized = filename.trimmed().toLower();
    normalized.remove(QRegularExpression(QStringLiteral("\\.(?:png|jpe?g|webp|gif)$"),
                                         QRegularExpression::CaseInsensitiveOption));
    normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    normalized.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    QStringList words = normalized.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    if (words.size() > 4) words = words.mid(0, 4);
    if (words.size() < 2) words << QStringLiteral("kindle");
    if (words.size() < 2) words << QStringLiteral("case");
    return words.join(QLatin1Char('-'));
}

bool validateSeoResult(const SeoResult &result, QString *error)
{
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (result.title.size() < 105 || result.title.size() > 125)
        return fail(QStringLiteral("Le titre doit contenir entre 105 et 125 caractères, cible exacte du prompt."));
    const int wordCount = result.description.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
    if (wordCount < 500)
        return fail(QStringLiteral("La description doit contenir au moins 500 mots."));
    const QRegularExpression leakedSection(QStringLiteral("\\bSECTION\\s*[1-5]\\b"),
                                             QRegularExpression::CaseInsensitiveOption);
    if (result.description.contains(leakedSection))
        return fail(QStringLiteral("La description révèle des libellés internes SECTION 1–5."));
    if (result.tags.size() != 13)
        return fail(QStringLiteral("La sortie doit contenir exactement 13 tags."));
    QSet<QString> uniqueTags;
    for (const auto &tag : result.tags) {
        if (tag.isEmpty() || tag.size() > 20)
            return fail(QStringLiteral("Chaque tag doit contenir de 1 à 20 caractères."));
        uniqueTags.insert(tag.toCaseFolded());
    }
    if (uniqueTags.size() != 13)
        return fail(QStringLiteral("Les 13 tags doivent être différents."));
    const QRegularExpression filenamePattern(QStringLiteral("^[a-z0-9]+(?:-[a-z0-9]+){1,3}$"));
    if (!filenamePattern.match(result.imageFilename).hasMatch())
        return fail(QStringLiteral("Le nom d'image doit contenir 2 à 4 mots minuscules séparés par des tirets."));
    if (result.altText.size() < 450 || result.altText.size() > 500)
        return fail(QStringLiteral("Le texte alternatif doit contenir entre 450 et 500 caractères."));
    auto normalizedWords = [](QString text) {
        text = text.toCaseFolded();
        text.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral(" "));
        return text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    };
    const QStringList titleLead = normalizedWords(result.title).mid(0, 3);
    const QStringList altLead = normalizedWords(result.altText).mid(0, 3);
    if (titleLead.size() < 3 || altLead != titleLead)
        return fail(QStringLiteral("Le texte alternatif doit commencer par les trois premiers mots-clés du titre."));
    return true;
}

QJsonObject seoResultToJson(const SeoResult &result)
{
    QJsonArray tags;
    for (const auto &tag : result.tags) tags.append(tag);
    return {{QStringLiteral("title"), result.title},
            {QStringLiteral("description"), result.description},
            {QStringLiteral("tags"), tags},
            {QStringLiteral("image_filename"), result.imageFilename},
            {QStringLiteral("alt_text"), result.altText}};
}

QString safeImageExtension(const QString &contentType, const QString &url)
{
    const QString type = contentType.toLower();
    if (type.contains(QStringLiteral("png"))) return QStringLiteral("png");
    if (type.contains(QStringLiteral("webp"))) return QStringLiteral("webp");
    if (type.contains(QStringLiteral("gif"))) return QStringLiteral("gif");
    const QString suffix = QUrl(url).path().section(QLatin1Char('.'), -1).toLower();
    if (suffix == QStringLiteral("png") || suffix == QStringLiteral("webp") ||
        suffix == QStringLiteral("gif") || suffix == QStringLiteral("jpeg")) return suffix;
    return QStringLiteral("jpg");
}
