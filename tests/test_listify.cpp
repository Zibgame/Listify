#include "listifycore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

class ListifyTests : public QObject {
    Q_OBJECT
private slots:
    void extractsStableListingId();
    void excludesOwnShop();
    void validatesSeoContract();
    void parsesStructuredResponse();
    void choosesSafeImageExtension();
};

void ListifyTests::extractsStableListingId()
{
    QCOMPARE(listingIdFromEtsyUrl(QStringLiteral("https://www.etsy.com/listing/123456/title?ref=x")),
             QStringLiteral("123456"));
    QCOMPARE(listingIdFromEtsyUrl(QStringLiteral("https://etsy.com/fr/listing/987/new")),
             QStringLiteral("987"));
    QVERIFY(listingIdFromEtsyUrl(QStringLiteral("https://example.com/item/123")).isEmpty());
}

void ListifyTests::excludesOwnShop()
{
    QVERIFY(isExcludedShop(QStringLiteral("TheKindleNook")));
    QVERIFY(isExcludedShop(QStringLiteral("  thekindlenook  ")));
    QVERIFY(!isExcludedShop(QStringLiteral("AnotherKindleShop")));
}

void ListifyTests::validatesSeoContract()
{
    SeoResult valid;
    valid.title = QString(110, QLatin1Char('t'));
    valid.description = QStringLiteral("mot ").repeated(500);
    for (int i = 0; i < 13; ++i) valid.tags << QStringLiteral("tag%1").arg(i);
    valid.imageFilename = QStringLiteral("kindle-case-floral");
    valid.altText = QString(475, QLatin1Char('a'));
    QVERIFY(validateSeoResult(valid));
    valid.tags.removeLast();
    QVERIFY(!validateSeoResult(valid));
}

void ListifyTests::parsesStructuredResponse()
{
    QJsonArray tags; for (int i = 0; i < 13; ++i) tags.append(QStringLiteral("tag%1").arg(i));
    const QJsonObject seo{{QStringLiteral("title"), QString(110, QLatin1Char('t'))},
                          {QStringLiteral("description"), QStringLiteral("mot ").repeated(500)},
                          {QStringLiteral("tags"), tags},
                          {QStringLiteral("image_filename"), QStringLiteral("kindle-case")},
                          {QStringLiteral("alt_text"), QString(475, QLatin1Char('a'))}};
    const QJsonObject response{{QStringLiteral("output"), QJsonArray{QJsonObject{
        {QStringLiteral("type"), QStringLiteral("message")},
        {QStringLiteral("content"), QJsonArray{QJsonObject{
            {QStringLiteral("type"), QStringLiteral("output_text")},
            {QStringLiteral("text"), QString::fromUtf8(QJsonDocument(seo).toJson(QJsonDocument::Compact))}}}}}}}};
    SeoResult parsed; QString error;
    QVERIFY2(parseSeoResponse(QJsonDocument(response).toJson(), &parsed, &error), qPrintable(error));
    QCOMPARE(parsed.tags.size(), 13);
}

void ListifyTests::choosesSafeImageExtension()
{
    QCOMPARE(safeImageExtension(QStringLiteral("image/png"), QString()), QStringLiteral("png"));
    QCOMPARE(safeImageExtension(QString(), QStringLiteral("https://x/a.jpeg")), QStringLiteral("jpeg"));
    QCOMPARE(safeImageExtension(QString(), QStringLiteral("https://x/no-extension")), QStringLiteral("jpg"));
}

QTEST_MAIN(ListifyTests)
#include "test_listify.moc"
