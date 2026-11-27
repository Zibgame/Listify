#include "listifyapp.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrlQuery>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#ifdef RGB
#undef RGB
#endif
#endif

using namespace ftxui;

namespace {
std::string utf8(const QString &value) { return value.toUtf8().toStdString(); }

bool writeBytes(const QString &path, const QByteArray &data, QString *error = nullptr)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error) *error = QStringLiteral("Impossible d'écrire %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    return true;
}

bool writeText(const QString &path, const QString &text, QString *error = nullptr)
{
    return writeBytes(path, text.toUtf8(), error);
}

QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

QString decodeBasicHtmlEntities(QString text)
{
    return text.replace(QStringLiteral("&quot;"), QStringLiteral("\""))
               .replace(QStringLiteral("&#39;"), QStringLiteral("'"))
               .replace(QStringLiteral("&apos;"), QStringLiteral("'"))
               .replace(QStringLiteral("&lt;"), QStringLiteral("<"))
               .replace(QStringLiteral("&gt;"), QStringLiteral(">"))
               .replace(QStringLiteral("&amp;"), QStringLiteral("&"));
}

bool openPath(const QString &path)
{
#ifdef Q_OS_WIN
    const auto native = QDir::toNativeSeparators(path).toStdWString();
    return reinterpret_cast<qintptr>(ShellExecuteW(nullptr, L"open", native.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
    Q_UNUSED(path); return false;
#endif
}

bool copyToClipboard(const QString &text)
{
#ifdef Q_OS_WIN
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    const std::wstring wide = text.toStdWString();
    const SIZE_T bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) { CloseClipboard(); return false; }
    void *target = GlobalLock(memory);
    memcpy(target, wide.c_str(), bytes);
    GlobalUnlock(memory);
    if (!SetClipboardData(CF_UNICODETEXT, memory)) { GlobalFree(memory); CloseClipboard(); return false; }
    CloseClipboard();
    return true;
#else
    Q_UNUSED(text); return false;
#endif
}
}

ListifyApp::ListifyApp(QObject *parent) : QObject(parent) {}

bool ListifyApp::initialize(const QStringList &arguments)
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    loadEnvironment();
    m_proxyConfigured = configureProxy();
    loadProfile();
    const int jobIndex = arguments.indexOf(QStringLiteral("--job"));
    if (jobIndex >= 0 && jobIndex + 1 < arguments.size()) loadJob(arguments.at(jobIndex + 1));
    const int urlIndex = arguments.indexOf(QStringLiteral("--url"));
    if (urlIndex >= 0 && urlIndex + 1 < arguments.size()) m_urlInput = arguments.at(urlIndex + 1).toUtf8().toStdString();
    const int artworkIndex = arguments.indexOf(QStringLiteral("--artwork"));
    if (artworkIndex >= 0 && artworkIndex + 1 < arguments.size()) m_artworkInput = arguments.at(artworkIndex + 1).toUtf8().toStdString();
    if (urlIndex >= 0 || artworkIndex >= 0) m_autoStart = true;
    if (!m_proxyConfigured) m_error = QStringLiteral("Proxy obligatoire absent : aucun accès réseau direct ne sera tenté.");
    return true;
}

int ListifyApp::run()
{
    auto screen = ScreenInteractive::Fullscreen();
    m_screen = &screen;
    auto urlInput = Input(&m_urlInput, "https://www.etsy.com/listing/...");
    auto artworkInput = Input(&m_artworkInput, "C:\\...\\Productfy_...png");
    auto keywords = Checkbox(" Utiliser keyword.txt", &m_useKeywords);
    auto generate = Button("  GÉNÉRER [G]  ", [this] { startWorkflow(); },
                           ButtonOption::Animated(Color::RGB(67, 211, 238)));
    auto open = Button("  OUVRIR OUTPUT [O]  ", [this] { openOutputFolder(); },
                       ButtonOption::Animated(Color::RGB(52, 211, 153)));
    auto quit = Button("  QUITTER [Q]  ", [&screen] { screen.Exit(); },
                       ButtonOption::Animated(Color::RGB(251, 191, 36)));
    auto controls = Container::Vertical({urlInput, artworkInput, keywords,
                                        Container::Horizontal({generate, open, quit})});

    auto renderer = Renderer(controls, [&, this] {
        const auto cyan = Color::RGB(67, 211, 238);
        const auto violet = Color::RGB(168, 85, 247);
        const auto green = Color::RGB(52, 211, 153);
        const auto amber = Color::RGB(251, 191, 36);
        const auto muted = Color::RGB(148, 163, 184);
        const auto panel = Color::RGB(20, 28, 45);
        Element header = hbox({text(" LISTIFY ") | bold | color(Color::Black) | bgcolor(violet),
                               text("  ETSY LISTING PIPELINE C++") | bold,
                               filler(),
                               text(m_proxyConfigured ? "● PROXY ACTIF " : "● RÉSEAU BLOQUÉ ") |
                                   bold | color(m_proxyConfigured ? green : Color::Red)});
        Element inputs = vbox({
            text("LIEN ETSY") | bold | color(cyan), urlInput->Render() | border,
            text("ARTWORK PRODUCTFY") | bold | color(violet), artworkInput->Render() | border,
            hbox({keywords->Render(), filler(), text("Profil : " + utf8(m_profileName)) | color(amber)}),
            separator(),
            hbox({generate->Render(), open->Render(), quit->Render()}) | center,
        });
        std::string title = m_result.title.isEmpty() ? "Aucun résultat pour le moment." : utf8(m_result.title);
        Element result = vbox({
            text("ÉTAT") | bold | color(cyan), paragraph(utf8(m_status)),
            m_error.isEmpty() ? text("") : paragraph(utf8(m_error)) | color(Color::Red) | bold,
            separator(),
            text("TITRE — touche 1 pour copier") | bold | color(green), paragraph(title),
            separator(),
            text("DOSSIER OUTPUT") | bold | color(violet),
            m_outputDir.isEmpty() ? text("Pas encore créé.") | color(muted)
                                  : paragraph(utf8(QDir::toNativeSeparators(m_outputDir))) | color(green),
            separator(),
            text("2 description  •  3 tags  •  4 alt text  •  5 nom image") | color(muted),
        });
        return vbox({header | size(HEIGHT, EQUAL, 1), separator(),
                     hbox({window(text(" ENTRÉE ") | bold | color(cyan), inputs) | bgcolor(panel) | flex,
                           separator(),
                           window(text(" RÉSULTAT ") | bold | color(violet), result) | bgcolor(panel) | flex}) | flex,
                     separator(), text(" G générer • O ouvrir • Q quitter • 1–5 copier ") | center | color(muted)}) |
               borderHeavy | bgcolor(Color::RGB(10, 15, 28));
    });

    auto component = CatchEvent(renderer, [&, this](Event event) {
        if (event == Event::Custom) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            if (m_autoStart) { m_autoStart = false; startWorkflow(); }
            return false;
        }
        if (!event.is_character()) return false;
        std::string key = event.character();
        if (key.size() == 1 && key[0] >= 'A' && key[0] <= 'Z') key[0] += 'a' - 'A';
        if (key == "g") { startWorkflow(); return true; }
        if (key == "o") { openOutputFolder(); return true; }
        if (key == "q") { screen.Exit(); return true; }
        if (key == "1" && !m_result.title.isEmpty()) { copyToClipboard(m_result.title); m_status = QStringLiteral("Titre copié."); refresh(); return true; }
        if (key == "2" && !m_result.description.isEmpty()) { copyToClipboard(m_result.description); m_status = QStringLiteral("Description copiée."); refresh(); return true; }
        if (key == "3" && !m_result.tags.isEmpty()) { copyToClipboard(m_result.tags.join(QStringLiteral(", "))); m_status = QStringLiteral("Tags copiés."); refresh(); return true; }
        if (key == "4" && !m_result.altText.isEmpty()) { copyToClipboard(m_result.altText); m_status = QStringLiteral("Texte alternatif copié."); refresh(); return true; }
        if (key == "5" && !m_result.imageFilename.isEmpty()) { copyToClipboard(m_result.imageFilename); m_status = QStringLiteral("Nom d'image copié."); refresh(); return true; }
        return false;
    });
    std::atomic_bool ticking{true};
    std::thread ticker([&] { while (ticking.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(80)); screen.PostEvent(Event::Custom); } });
    screen.Loop(component);
    ticking = false; ticker.join(); m_screen = nullptr;
    return 0;
}

void ListifyApp::startWorkflow()
{
    if (m_busy) { m_error = QStringLiteral("Un traitement est déjà en cours."); refresh(); return; }
    if (!m_proxyConfigured) { m_error = QStringLiteral("Proxy obligatoire absent. Connexion refusée."); refresh(); return; }
    const QString url = QString::fromUtf8(m_urlInput);
    m_listingId = listingIdFromEtsyUrl(url);
    if (m_listingId.isEmpty()) { m_error = QStringLiteral("Lien Etsy invalide : /listing/<id> attendu."); refresh(); return; }
    if (!QFileInfo::exists(QString::fromUtf8(m_artworkInput))) { m_error = QStringLiteral("Artwork Productfy introuvable."); refresh(); return; }
    if (apiKey().isEmpty()) { m_error = QStringLiteral("Clé Etsy absente du .env."); refresh(); return; }
    if (qEnvironmentVariable("OPENAI_API_KEY").trimmed().isEmpty()) { m_error = QStringLiteral("OPENAI_API_KEY absente."); refresh(); return; }
    if (m_prompt.trimmed().isEmpty()) { m_error = QStringLiteral("Prompt SEO du profil introuvable."); refresh(); return; }
    m_busy = true; m_error.clear(); m_result = {}; m_images.clear(); m_imageUrls.clear(); m_listing = {}; m_inventory = {};
    m_outputDir.clear(); m_shopName.clear(); m_downloadIndex = 0; m_aiAttempt = 0;
    m_totalInputTokens = 0; m_totalCachedTokens = 0; m_totalOutputTokens = 0;
    m_status = QStringLiteral("Récupération de la fiche Etsy %1...").arg(m_listingId); refresh();
    fetchListing();
}

void ListifyApp::fetchListing()
{
    const QUrl url(QStringLiteral("https://openapi.etsy.com/v3/application/listings/%1").arg(m_listingId));
    QNetworkReply *reply = m_network.get(etsyRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray data = reply->readAll(); const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300; reply->deleteLater();
        if (!ok) { fail(QStringLiteral("Etsy listing"), QStringLiteral("HTTP %1").arg(status)); return; }
        m_listing = QJsonDocument::fromJson(data).object();
        const qint64 shopId = m_listing.value(QStringLiteral("shop_id")).toVariant().toLongLong();
        if (shopId <= 0) { fail(QStringLiteral("Etsy listing"), QStringLiteral("Vendeur inconnu.")); return; }
        m_status = QStringLiteral("Fiche trouvée. Vérification de la boutique..."); refresh(); fetchShop(shopId);
    });
}

void ListifyApp::fetchShop(qint64 shopId)
{
    QNetworkReply *reply = m_network.get(etsyRequest(QUrl(QStringLiteral("https://openapi.etsy.com/v3/application/shops/%1").arg(shopId))));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray data = reply->readAll(); const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300; reply->deleteLater();
        if (!ok) { fail(QStringLiteral("Etsy shop"), QStringLiteral("HTTP %1").arg(status)); return; }
        m_shopName = QJsonDocument::fromJson(data).object().value(QStringLiteral("shop_name")).toString().trimmed();
        if (m_shopName.isEmpty()) { fail(QStringLiteral("Etsy shop"), QStringLiteral("Nom de boutique inconnu.")); return; }
        if (isExcludedShop(m_shopName)) { fail(QStringLiteral("Etsy shop"), QStringLiteral("TheKindleNook est systématiquement exclue.")); return; }
        m_status = QStringLiteral("Boutique %1 vérifiée. Lecture des variantes réelles...").arg(m_shopName); refresh(); fetchInventory();
    });
}

void ListifyApp::fetchInventory()
{
    const QUrl url(QStringLiteral("https://openapi.etsy.com/v3/application/listings/%1/inventory").arg(m_listingId));
    QNetworkReply *reply = m_network.get(etsyRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        reply->deleteLater();
        if (ok) m_inventory = QJsonDocument::fromJson(data).object();
        else m_inventory = QJsonObject{{QStringLiteral("unavailable"), true},
                                       {QStringLiteral("http_status"), status}};
        m_status = ok ? QStringLiteral("Variantes Etsy récupérées. Lecture des images...")
                      : QStringLiteral("Variantes indisponibles. Lecture des images...");
        refresh(); fetchImageIndex();
    });
}

void ListifyApp::fetchImageIndex()
{
    QNetworkReply *reply = m_network.get(etsyRequest(QUrl(QStringLiteral("https://openapi.etsy.com/v3/application/listings/%1/images").arg(m_listingId))));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray data = reply->readAll(); const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300; reply->deleteLater();
        if (!ok) { fail(QStringLiteral("Images Etsy"), QStringLiteral("HTTP %1").arg(status)); return; }
        auto results = QJsonDocument::fromJson(data).object().value(QStringLiteral("results")).toArray();
        QVector<QPair<int,QUrl>> ranked;
        for (const auto &value : results) {
            const auto object = value.toObject();
            QString imageUrl = object.value(QStringLiteral("url_fullxfull")).toString();
            if (imageUrl.isEmpty()) imageUrl = object.value(QStringLiteral("url_570xN")).toString();
            if (!imageUrl.isEmpty()) ranked.push_back({object.value(QStringLiteral("rank")).toInt(999), QUrl(imageUrl)});
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        for (int i = 0; i < qMin(3, ranked.size()); ++i) m_imageUrls << ranked.at(i).second;
        if (m_imageUrls.isEmpty()) { fail(QStringLiteral("Images Etsy"), QStringLiteral("Aucune image de listing disponible.")); return; }
        m_status = QStringLiteral("Téléchargement de %1 image(s) source via le proxy...").arg(m_imageUrls.size()); refresh(); downloadNextImage();
    });
}

void ListifyApp::downloadNextImage()
{
    if (m_downloadIndex >= m_imageUrls.size()) {
        if (!prepareOutput()) return;
        m_status = QStringLiteral("Sources archivées. Génération SEO avec %1 image(s) Etsy + artwork...").arg(m_images.size()); refresh();
        callOpenAi(); return;
    }
    const QUrl url = m_imageUrls.at(m_downloadIndex);
    QNetworkRequest request(url); request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Listify/2.0")); request.setTransferTimeout(120000);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError || data.isEmpty()) { const QString message = reply->errorString(); reply->deleteLater(); fail(QStringLiteral("Image Etsy"), message); return; }
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString(); reply->deleteLater();
        m_images.push_back({data, safeImageExtension(contentType, url.toString()), url.toString()});
        ++m_downloadIndex; m_status = QStringLiteral("Images téléchargées : %1/%2").arg(m_downloadIndex).arg(m_imageUrls.size()); refresh(); downloadNextImage();
    });
}

bool ListifyApp::prepareOutput()
{
    m_outputDir = QDir(projectRoot()).filePath(QStringLiteral("output/%1").arg(m_listingId));
    const QStringList dirs{m_outputDir, QDir(m_outputDir).filePath(QStringLiteral("source")),
                           QDir(m_outputDir).filePath(QStringLiteral("input")),
                           QDir(m_outputDir).filePath(QStringLiteral("result")),
                           QDir(m_outputDir).filePath(QStringLiteral("debug"))};
    for (const auto &dir : dirs) if (!QDir().mkpath(dir)) { fail(QStringLiteral("Output"), QStringLiteral("Impossible de créer %1").arg(dir)); return false; }
    QJsonObject archive = m_listing;
    archive.insert(QStringLiteral("resolved_shop_name"), m_shopName);
    archive.insert(QStringLiteral("source_url"), QString::fromUtf8(m_urlInput));
    archive.insert(QStringLiteral("collected_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    QString error;
    if (!writeBytes(QDir(m_outputDir).filePath(QStringLiteral("source/listing.json")), QJsonDocument(archive).toJson(QJsonDocument::Indented), &error)) { fail(QStringLiteral("Output"), error); return false; }
    writeBytes(QDir(m_outputDir).filePath(QStringLiteral("source/inventory.json")),
               QJsonDocument(m_inventory).toJson(QJsonDocument::Indented));
    QJsonArray imageManifest;
    for (int i = 0; i < m_images.size(); ++i) {
        const auto &image = m_images.at(i);
        const QString name = QStringLiteral("listing_%1.%2").arg(i + 1, 2, 10, QLatin1Char('0')).arg(image.extension);
        if (!writeBytes(QDir(m_outputDir).filePath(QStringLiteral("source/") + name), image.bytes, &error)) { fail(QStringLiteral("Output"), error); return false; }
        imageManifest.append(QJsonObject{{QStringLiteral("file"), name}, {QStringLiteral("source_url"), image.sourceUrl}});
    }
    writeBytes(QDir(m_outputDir).filePath(QStringLiteral("source/images.json")), QJsonDocument(imageManifest).toJson(QJsonDocument::Indented));
    QFile artwork(QString::fromUtf8(m_artworkInput));
    if (!artwork.open(QIODevice::ReadOnly) || !writeBytes(QDir(m_outputDir).filePath(QStringLiteral("source/productfy_artwork.png")), artwork.readAll(), &error)) { fail(QStringLiteral("Output"), error); return false; }
    writeText(QDir(m_outputDir).filePath(QStringLiteral("input/seo_prompt.txt")), m_prompt);
    writeText(QDir(m_outputDir).filePath(QStringLiteral("input/listing_style.txt")), m_styleRules);
    writeText(QDir(m_outputDir).filePath(QStringLiteral("input/product_brief.txt")), productBrief());
    if (m_useKeywords) writeText(QDir(m_outputDir).filePath(QStringLiteral("input/keywords.txt")), m_keywords);
    return true;
}

void ListifyApp::callOpenAi(const QString &correction)
{
    ++m_aiAttempt;
    QFile artwork(QDir(m_outputDir).filePath(QStringLiteral("source/productfy_artwork.png")));
    artwork.open(QIODevice::ReadOnly);
    QJsonArray content;
    QString payload = productBrief();
    if (m_useKeywords && !m_keywords.trimmed().isEmpty())
        payload += QStringLiteral("\n\n=== KEYWORD TXT FILE ===\n") + m_keywords;
    payload += QStringLiteral("\n\nGenerate the five requested outputs now. Treat the new Productfy artwork as the product design to describe. Use the competitor listing only for verified physical features, compatibility and variants. Return only the imposed JSON object.");
    if (!correction.isEmpty())
        payload += QStringLiteral("\n\nCORRECTION OBLIGATOIRE APRÈS VALIDATION : %1 Recrée intégralement les cinq champs en respectant toutes les limites.").arg(correction);
    content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("input_text")}, {QStringLiteral("text"), payload}});
    content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("input_text")},
                               {QStringLiteral("text"), QStringLiteral("IMAGE A — NEW PRODUCTFY ARTWORK. This is the new design being listed and the main source for visual theme, colors and motifs.")}});
    content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("input_image")},
                               {QStringLiteral("image_url"), QStringLiteral("data:image/png;base64,") + QString::fromLatin1(artwork.readAll().toBase64())},
                               {QStringLiteral("detail"), QStringLiteral("high")}});
    for (int i = 0; i < m_images.size(); ++i) {
        const auto &image = m_images.at(i);
        content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("input_text")},
                                   {QStringLiteral("text"), QStringLiteral("COMPETITOR LISTING IMAGE %1 — use only as supporting evidence for the physical case, presentation and available features. Do not let it override the new artwork.").arg(i + 1)}});
        const QString mime = image.extension == QStringLiteral("png") ? QStringLiteral("image/png") :
                             image.extension == QStringLiteral("webp") ? QStringLiteral("image/webp") : QStringLiteral("image/jpeg");
        content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("input_image")},
                                   {QStringLiteral("image_url"), QStringLiteral("data:%1;base64,%2").arg(mime, QString::fromLatin1(image.bytes.toBase64()))},
                                   {QStringLiteral("detail"), QStringLiteral("high")}});
    }
    const QJsonObject schema{{QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), QJsonObject{
            {QStringLiteral("title"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                {QStringLiteral("minLength"), 105}, {QStringLiteral("maxLength"), 125}}},
            {QStringLiteral("description"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
            {QStringLiteral("tags"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                {QStringLiteral("minItems"), 13}, {QStringLiteral("maxItems"), 13},
                {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 20}}}}},
            {QStringLiteral("image_filename"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
            {QStringLiteral("alt_text"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                {QStringLiteral("minLength"), 450}, {QStringLiteral("maxLength"), 500}}}}},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("title"), QStringLiteral("description"), QStringLiteral("tags"), QStringLiteral("image_filename"), QStringLiteral("alt_text")}},
        {QStringLiteral("additionalProperties"), false}};
    m_usedModel = qEnvironmentVariable("OPENAI_SEO_MODEL", QStringLiteral("gpt-6.2-astra")).trimmed();
    m_reasoningEffort = qEnvironmentVariable("OPENAI_REASONING_EFFORT", QStringLiteral("high")).trimmed().toLower();
    const QString instructions = m_prompt + QStringLiteral("\n\n") + m_styleRules;
    const QJsonObject body{{QStringLiteral("model"), m_usedModel},
        {QStringLiteral("instructions"), instructions}, {QStringLiteral("store"), false},
        {QStringLiteral("reasoning"), QJsonObject{{QStringLiteral("effort"), m_reasoningEffort}}},
        {QStringLiteral("max_output_tokens"), 10000},
        {QStringLiteral("input"), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), content}}}},
        {QStringLiteral("text"), QJsonObject{{QStringLiteral("format"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("json_schema")}, {QStringLiteral("name"), QStringLiteral("etsy_listing")},
            {QStringLiteral("strict"), true}, {QStringLiteral("schema"), schema}}}}}};
    QNetworkRequest request(QUrl(QStringLiteral("https://api.openai.com/v1/responses")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + qEnvironmentVariable("OPENAI_API_KEY").toUtf8());
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Listify/2.0")); request.setTransferTimeout(300000);
    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray data = reply->readAll(); const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString networkError = reply->errorString();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300; reply->deleteLater();
        const auto responseObject = QJsonDocument::fromJson(data).object();
        const auto usage = responseObject.value(QStringLiteral("usage")).toObject();
        m_totalInputTokens += usage.value(QStringLiteral("input_tokens")).toInteger();
        m_totalCachedTokens += usage.value(QStringLiteral("input_tokens_details")).toObject().value(QStringLiteral("cached_tokens")).toInteger();
        m_totalOutputTokens += usage.value(QStringLiteral("output_tokens")).toInteger();
        writeBytes(QDir(m_outputDir).filePath(QStringLiteral("debug/openai_response_attempt_%1.json").arg(m_aiAttempt)), data);
        writeBytes(QDir(m_outputDir).filePath(QStringLiteral("debug/openai_response.json")), data);
        if (!ok) {
            const auto error = QJsonDocument::fromJson(data).object().value(QStringLiteral("error")).toObject();
            fail(QStringLiteral("OpenAI"), QStringLiteral("HTTP %1 : %2").arg(status).arg(error.value(QStringLiteral("message")).toString(networkError))); return;
        }
        SeoResult result; QString error;
        if (!parseSeoResponse(data, &result, &error)) {
            if (m_aiAttempt < 2) {
                m_status = QStringLiteral("Sortie hors contrat (%1). Nouvelle tentative corrective 2/2...").arg(error);
                refresh(); callOpenAi(error); return;
            }
            fail(QStringLiteral("Validation SEO"), error); return;
        }
        if (!saveResults(data, result)) return;
        m_result = result; m_busy = false; m_error.clear();
        m_status = QStringLiteral("Listing SEO terminé. Résultats rangés et dossier ouvert."); refresh(); openOutputFolder();
    });
}

bool ListifyApp::saveResults(const QByteArray &rawResponse, const SeoResult &result)
{
    const QString resultDir = QDir(m_outputDir).filePath(QStringLiteral("result")); QString error;
    const QString tags = result.tags.join(QStringLiteral(", "));
    if (!writeText(QDir(resultDir).filePath(QStringLiteral("name.txt")), result.title, &error) ||
        !writeText(QDir(resultDir).filePath(QStringLiteral("title.txt")), result.title, &error) ||
        !writeText(QDir(resultDir).filePath(QStringLiteral("description.txt")), result.description, &error) ||
        !writeText(QDir(resultDir).filePath(QStringLiteral("tags.txt")), tags, &error) ||
        !writeText(QDir(resultDir).filePath(QStringLiteral("alt_text.txt")), result.altText, &error) ||
        !writeText(QDir(resultDir).filePath(QStringLiteral("image_filename.txt")), result.imageFilename, &error) ||
        !writeBytes(QDir(resultDir).filePath(QStringLiteral("listing.json")), QJsonDocument(seoResultToJson(result)).toJson(QJsonDocument::Indented), &error)) {
        fail(QStringLiteral("Output"), error); return false;
    }
    const QString markdown = QStringLiteral("# %1\n\n## Description\n\n%2\n\n## Tags\n\n%3\n\n## Image filename\n\n`%4`\n\n## Alt text\n\n%5\n")
        .arg(result.title, result.description, tags, result.imageFilename, result.altText);
    writeText(QDir(resultDir).filePath(QStringLiteral("listing.md")), markdown);
    const QString chatGptOutput = QStringLiteral("TITLE\n%1\n\nDESCRIPTION\n%2\n\nTAGS\n%3\n\nIMAGE FILENAME\n%4\n\nALT TEXT\n%5\n")
        .arg(result.title, result.description, tags, result.imageFilename, result.altText);
    writeText(QDir(resultDir).filePath(QStringLiteral("chatgpt_output.txt")), chatGptOutput);
    const auto responseObject = QJsonDocument::fromJson(rawResponse).object();
    double inputRate = 0.0, cachedRate = 0.0, outputRate = 0.0;
    if (m_usedModel.startsWith(QStringLiteral("gpt-6.2-astra"))) { inputRate = 8.0; cachedRate = 0.4; outputRate = 40.0; }
    else if (m_usedModel.startsWith(QStringLiteral("gpt-6.2-sol"))) { inputRate = 2.0; cachedRate = 0.1; outputRate = 10.0; }
    else if (m_usedModel.startsWith(QStringLiteral("gpt-5.5"))) { inputRate = 5.0; cachedRate = 0.5; outputRate = 30.0; }
    const qint64 uncachedTokens = qMax<qint64>(0, m_totalInputTokens - m_totalCachedTokens);
    const double estimatedCost = (uncachedTokens * inputRate + m_totalCachedTokens * cachedRate +
                                  m_totalOutputTokens * outputRate) / 1000000.0;
    QJsonObject generation{{QStringLiteral("model"), m_usedModel},
        {QStringLiteral("reasoning_effort"), m_reasoningEffort},
        {QStringLiteral("attempts"), m_aiAttempt},
        {QStringLiteral("response_id"), responseObject.value(QStringLiteral("id"))},
        {QStringLiteral("input_tokens_total"), m_totalInputTokens},
        {QStringLiteral("cached_input_tokens_total"), m_totalCachedTokens},
        {QStringLiteral("output_tokens_total"), m_totalOutputTokens}};
    if (inputRate > 0.0) generation.insert(QStringLiteral("estimated_standard_cost_usd"), estimatedCost);
    writeBytes(QDir(resultDir).filePath(QStringLiteral("generation_info.json")),
               QJsonDocument(generation).toJson(QJsonDocument::Indented));
    const QString ps = QStringLiteral(
        "$root = Join-Path $PSScriptRoot 'result'\n"
        "$items = @('title.txt','description.txt','tags.txt','alt_text.txt','image_filename.txt')\n"
        "while ($true) { Clear-Host; Write-Host 'LISTIFY - COPIER UN RESULTAT'; for ($i=0;$i -lt $items.Count;$i++){Write-Host \"$($i+1). $($items[$i])\"}; Write-Host 'O. Ouvrir le dossier'; Write-Host 'Q. Quitter'; $c=Read-Host 'Choix'; if($c -match '^[1-5]$'){Get-Content -Raw (Join-Path $root $items[[int]$c-1]) | Set-Clipboard; Write-Host 'Copie dans le presse-papiers'; Start-Sleep 1} elseif($c -match '^[oO]$'){Start-Process $root} elseif($c -match '^[qQ]$'){break}}\n");
    writeText(QDir(m_outputDir).filePath(QStringLiteral("copy_results.ps1")), ps);
    writeText(QDir(m_outputDir).filePath(QStringLiteral("copy_results.cmd")), QStringLiteral("@echo off\r\npowershell -NoProfile -ExecutionPolicy Bypass -File \"%~dp0copy_results.ps1\"\r\n"));
    return true;
}

void ListifyApp::openOutputFolder()
{
    if (m_outputDir.isEmpty() || !QFileInfo::exists(m_outputDir)) { m_error = QStringLiteral("Aucun dossier output disponible."); refresh(); return; }
    if (!openPath(m_outputDir)) m_error = QStringLiteral("Windows n'a pas pu ouvrir le dossier output.");
}

void ListifyApp::loadEnvironment()
{
    const QString root = projectRoot();
    const QStringList paths{QDir(root).filePath(QStringLiteral(".env")),
                            QDir(root).filePath(QStringLiteral("../Orderify/.env")),
                            QStringLiteral("C:/Users/faniz/Documents/Kindle_Case/Mockify/.env")};
    const QSet<QString> allowed{QStringLiteral("ETSY_API_KEY"), QStringLiteral("ETSY_SECRET"),
        QStringLiteral("PROXY_HOST"), QStringLiteral("PROXY_PORT"), QStringLiteral("PROXY_USERNAME"),
        QStringLiteral("PROXY_PASSWORD"), QStringLiteral("ETSY_ACCESS_TOKEN"),
        QStringLiteral("OPENAI_API_KEY"), QStringLiteral("OPENAI_SEO_MODEL"),
        QStringLiteral("OPENAI_REASONING_EFFORT"), QStringLiteral("LISTIFY_SEO_PROMPT"),
        QStringLiteral("LISTIFY_KEYWORDS")};
    for (const auto &path : paths) {
        QFile file(path); if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QTextStream stream(&file);
        while (!stream.atEnd()) {
            QString line = stream.readLine().trimmed(); if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
            const auto equals = line.indexOf(QLatin1Char('=')); if (equals <= 0) continue;
            const QString name = line.left(equals).trimmed(); if (!allowed.contains(name) || !qEnvironmentVariableIsEmpty(name.toUtf8().constData())) continue;
            QString value = line.mid(equals + 1).trimmed();
            if (value.size() >= 2 && ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))) ||
                                      (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))))) value = value.mid(1, value.size() - 2);
            qputenv(name.toUtf8(), value.toUtf8());
        }
    }
}

bool ListifyApp::configureProxy()
{
    bool ok = false; const quint16 port = qEnvironmentVariable("PROXY_PORT").toUShort(&ok);
    const QString host = qEnvironmentVariable("PROXY_HOST").trimmed(); if (!ok || !port || host.isEmpty()) return false;
    m_network.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, host, port,
                                    qEnvironmentVariable("PROXY_USERNAME"), qEnvironmentVariable("PROXY_PASSWORD")));
    return true;
}

void ListifyApp::loadProfile()
{
    const QString profileDir = QDir(projectRoot()).filePath(QStringLiteral("profiles/%1").arg(m_profileName));
    const auto manifest = QJsonDocument::fromJson(readText(QDir(profileDir).filePath(QStringLiteral("profile.json"))).toUtf8()).object();
    auto resolve = [profileDir](const QString &value) { return QFileInfo(value).isAbsolute() ? value : QDir(profileDir).filePath(value); };
    QString promptPath = resolve(manifest.value(QStringLiteral("seo_prompt")).toString(QStringLiteral("seo_generator.txt")));
    QString keywordsPath = resolve(manifest.value(QStringLiteral("keywords")).toString(QStringLiteral("keyword.txt")));
    QString stylePath = resolve(manifest.value(QStringLiteral("style_rules")).toString(QStringLiteral("listing_style.txt")));
    const QString externalPrompt = qEnvironmentVariable("LISTIFY_SEO_PROMPT",
        QStringLiteral("C:/Users/faniz/Documents/Kindle_Case/seo_generator.txt"));
    const QString externalKeywords = qEnvironmentVariable("LISTIFY_KEYWORDS",
        QStringLiteral("C:/Users/faniz/Documents/Kindle_Case/keyword.txt"));
    if (m_profileName == QStringLiteral("kindle_case") && QFileInfo(externalPrompt).isFile()) promptPath = externalPrompt;
    if (m_profileName == QStringLiteral("kindle_case") && QFileInfo(externalKeywords).isFile()) keywordsPath = externalKeywords;
    m_prompt = readText(promptPath); m_keywords = readText(keywordsPath); m_styleRules = readText(stylePath);
}

QString ListifyApp::productBrief() const
{
    const auto stringList = [](const QJsonValue &value) {
        QStringList values;
        for (const auto &item : value.toArray()) values << item.toString();
        return values.join(QStringLiteral(", "));
    };
    QStringList variants;
    QSet<QString> seenVariants;
    for (const auto &productValue : m_inventory.value(QStringLiteral("products")).toArray()) {
        QStringList values;
        for (const auto &propertyValue : productValue.toObject().value(QStringLiteral("property_values")).toArray()) {
            const auto property = propertyValue.toObject();
            const QString name = property.value(QStringLiteral("property_name")).toString();
            const QString options = stringList(property.value(QStringLiteral("values")));
            if (!name.isEmpty() && !options.isEmpty()) values << name + QStringLiteral(": ") + options;
        }
        const QString variant = values.join(QStringLiteral(" | "));
        if (!variant.isEmpty() && !seenVariants.contains(variant)) { seenVariants.insert(variant); variants << variant; }
    }
    QString brief = QStringLiteral(
        "=== PRODUCT INPUTS ===\n"
        "Product name / competitor title:\n%1\n\n"
        "Supplier / competitor description (raw context only; outperform it):\n%2\n\n"
        "Verified shop: %3\n"
        "Listing URL: %4\n"
        "Existing competitor tags: %5\n"
        "Materials returned by Etsy: %6\n\n"
        "Verified Device / Style variant catalog:\n%7")
        .arg(m_listing.value(QStringLiteral("title")).toString(),
             decodeBasicHtmlEntities(m_listing.value(QStringLiteral("description")).toString()),
             m_shopName, QString::fromUtf8(m_urlInput),
             stringList(m_listing.value(QStringLiteral("tags"))),
             stringList(m_listing.value(QStringLiteral("materials"))),
             variants.isEmpty() ? QStringLiteral("Not available from Etsy; do not invent variants.")
                                : variants.join(QLatin1Char('\n')));
    return brief;
}

void ListifyApp::loadJob(const QString &path)
{
    const auto job = QJsonDocument::fromJson(readText(path).toUtf8()).object();
    m_urlInput = job.value(QStringLiteral("listing_url")).toString().toUtf8().toStdString();
    m_artworkInput = job.value(QStringLiteral("artwork_path")).toString().toUtf8().toStdString();
    m_profileName = job.value(QStringLiteral("profile")).toString(QStringLiteral("kindle_case"));
    m_useKeywords = job.value(QStringLiteral("use_keywords")).toBool(true);
    loadProfile(); m_autoStart = true; m_status = QStringLiteral("Job Productfy chargé. Démarrage automatique...");
}

QString ListifyApp::projectRoot() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    if (!dir.exists(QStringLiteral("profiles"))) {
        QDir parent(dir);
        if (parent.cdUp() && parent.exists(QStringLiteral("profiles"))) dir = parent;
    }
    return dir.absolutePath();
}

QString ListifyApp::apiKey() const
{
    QString key = qEnvironmentVariable("ETSY_API_KEY").trimmed(); const QString secret = qEnvironmentVariable("ETSY_SECRET").trimmed();
    if (!key.isEmpty() && !secret.isEmpty() && !key.contains(QLatin1Char(':'))) key += QLatin1Char(':') + secret;
    return key;
}

QNetworkRequest ListifyApp::etsyRequest(const QUrl &url) const
{
    QNetworkRequest request(url); request.setRawHeader("x-api-key", apiKey().toUtf8());
    const QByteArray accessToken = qEnvironmentVariable("ETSY_ACCESS_TOKEN").trimmed().toUtf8();
    if (!accessToken.isEmpty()) request.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Listify/2.0")); request.setTransferTimeout(120000); return request;
}

void ListifyApp::fail(const QString &stage, const QString &message)
{
    m_busy = false; m_status = QStringLiteral("Échec à l'étape %1.").arg(stage); m_error = message; refresh();
}

void ListifyApp::refresh() { if (m_screen) m_screen->PostEvent(Event::Custom); }
