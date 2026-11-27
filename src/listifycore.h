#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

struct SeoResult {
    QString title;
    QString description;
    QStringList tags;
    QString imageFilename;
    QString altText;
};

QString listingIdFromEtsyUrl(const QString &url);
bool isExcludedShop(const QString &shopName);
bool parseSeoResponse(const QByteArray &responseData, SeoResult *result, QString *error = nullptr);
bool validateSeoResult(const SeoResult &result, QString *error = nullptr);
QString normalizeImageFilename(const QString &filename);
QJsonObject seoResultToJson(const SeoResult &result);
QString safeImageExtension(const QString &contentType, const QString &url);
