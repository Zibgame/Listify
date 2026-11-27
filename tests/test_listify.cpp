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
    void normalizesGeneratedFilename();
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
    valid.title = QStringLiteral("Magnetic Kindle Case ") + QString(90, QLatin1Char('x'));
    valid.description = QStringLiteral("mot ").repeated(500);
    for (int i = 0; i < 13; ++i) valid.tags << QStringLiteral("tag%1").arg(i);
    valid.imageFilename = QStringLiteral("kindle-case-floral");
    valid.altText = QStringLiteral("Magnetic Kindle Case ") + QString(455, QLatin1Char('a'));
    QVERIFY(validateSeoResult(valid));
    SeoResult punctuation = valid;
    punctuation.title = QStringLiteral("Kindle Paperwhite Case, ") + QString(87, QLatin1Char('x'));
    punctuation.altText = QStringLiteral("Kindle Paperwhite Case design ") + QString(445, QLatin1Char('a'));
    QVERIFY(validateSeoResult(punctuation));
    valid.tags.removeLast();
    QVERIFY(!validateSeoResult(valid));
    valid.tags << QStringLiteral("tag12");
    valid.description = QStringLiteral("mot ").repeated(500) + QStringLiteral(" SECTION 3 ");
    QVERIFY(!validateSeoResult(valid));
    valid.description = QStringLiteral("mot ").repeated(500);
    valid.imageFilename = QStringLiteral("Too_Many_Invalid Words Here Now");
    QVERIFY(!validateSeoResult(valid));
}

void ListifyTests::parsesStructuredResponse()
{
    QJsonArray tags; for (int i = 0; i < 13; ++i) tags.append(QStringLiteral("tag%1").arg(i));
    const QJsonObject seo{{QStringLiteral("title"), QStringLiteral("Kindle Paperwhite Case, ") + QString(87, QLatin1Char('x'))},
                          {QStringLiteral("description"), QStringLiteral("mot ").repeated(500)},
                          {QStringLiteral("tags"), tags},
                          {QStringLiteral("image_filename"), QStringLiteral("kindle-paperwhite-case.jpg")},
                          {QStringLiteral("alt_text"), QStringLiteral("Kindle Paperwhite Case design ") + QString(445, QLatin1Char('a'))}};
    const QJsonObject response{{QStringLiteral("output"), QJsonArray{QJsonObject{
        {QStringLiteral("type"), QStringLiteral("message")},
        {QStringLiteral("content"), QJsonArray{QJsonObject{
            {QStringLiteral("type"), QStringLiteral("output_text")},
            {QStringLiteral("text"), QString::fromUtf8(QJsonDocument(seo).toJson(QJsonDocument::Compact))}}}}}}}};
    SeoResult parsed; QString error;
    QVERIFY2(parseSeoResponse(QJsonDocument(response).toJson(), &parsed, &error), qPrintable(error));
    QCOMPARE(parsed.tags.size(), 13);
    QCOMPARE(parsed.imageFilename, QStringLiteral("kindle-paperwhite-case"));
}

void ListifyTests::choosesSafeImageExtension()
{
    QCOMPARE(safeImageExtension(QStringLiteral("image/png"), QString()), QStringLiteral("png"));
    QCOMPARE(safeImageExtension(QString(), QStringLiteral("https://x/a.jpeg")), QStringLiteral("jpeg"));
    QCOMPARE(safeImageExtension(QString(), QStringLiteral("https://x/no-extension")), QStringLiteral("jpg"));
}

void ListifyTests::normalizesGeneratedFilename()
{
    QCOMPARE(normalizeImageFilename(QStringLiteral("kindle-paperwhite-case.jpg")),
             QStringLiteral("kindle-paperwhite-case"));
    QCOMPARE(normalizeImageFilename(QStringLiteral("Embroidered Floral Kindle Sleeve Cover PNG.png")),
             QStringLiteral("embroidered-floral-kindle-sleeve"));
    QCOMPARE(normalizeImageFilename(QStringLiteral("floral")), QStringLiteral("floral-kindle"));
}

QTEST_MAIN(ListifyTests)
#include "test_listify.moc"
