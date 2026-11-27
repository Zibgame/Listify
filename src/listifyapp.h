#pragma once

#include "listifycore.h"

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <ftxui/component/screen_interactive.hpp>

class ListifyApp : public QObject {
    Q_OBJECT
public:
    explicit ListifyApp(QObject *parent = nullptr);
    bool initialize(const QStringList &arguments);
    int run();

private:
    struct DownloadedImage { QByteArray bytes; QString extension; QString sourceUrl; };

    void loadEnvironment();
    bool configureProxy();
    void loadProfile();
    void loadJob(const QString &path);
    void startWorkflow();
    void fetchListing();
    void fetchShop(qint64 shopId);
    void fetchInventory();
    void fetchImageIndex();
    void downloadNextImage();
    void callOpenAi(const QString &correction = QString());
    bool prepareOutput();
    bool saveResults(const QByteArray &rawResponse, const SeoResult &result);
    void openOutputFolder();
    void refresh();
    QString projectRoot() const;
    QString apiKey() const;
    QString productBrief() const;
    QNetworkRequest etsyRequest(const QUrl &url) const;
    void fail(const QString &stage, const QString &message);

    QNetworkAccessManager m_network;
    ftxui::ScreenInteractive *m_screen = nullptr;
    std::string m_urlInput;
    std::string m_artworkInput;
    QString m_listingId;
    QString m_profileName = QStringLiteral("kindle_case");
    QString m_prompt;
    QString m_keywords;
    QString m_styleRules;
    QString m_usedModel;
    QString m_reasoningEffort;
    QString m_status = QStringLiteral("Prêt. Collez un lien Etsy ou ouvrez un job Productfy.");
    QString m_error;
    QString m_outputDir;
    QString m_shopName;
    QJsonObject m_listing;
    QJsonObject m_inventory;
    QVector<QUrl> m_imageUrls;
    QVector<DownloadedImage> m_images;
    int m_downloadIndex = 0;
    int m_aiAttempt = 0;
    qint64 m_totalInputTokens = 0;
    qint64 m_totalCachedTokens = 0;
    qint64 m_totalOutputTokens = 0;
    bool m_useKeywords = true;
    bool m_proxyConfigured = false;
    bool m_busy = false;
    bool m_autoStart = false;
    SeoResult m_result;
};
