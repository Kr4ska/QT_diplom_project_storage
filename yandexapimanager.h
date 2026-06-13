#ifndef YANDEXAPIMANAGER_H
#define YANDEXAPIMANAGER_H

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class YandexApiManager : public QObject
{
    Q_OBJECT
public:
    explicit YandexApiManager(QObject *parent = nullptr);

    Q_INVOKABLE void sendRequest(const QString &prompt, const QString &apiKey, const QString &folderId, const QString &modelUri, double temperature);

signals:
    void responseReceived(const QString &jsonResponse);
    void errorOccurred(const QString &errorMessage);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_networkManager;
};

#endif // YANDEXAPIMANAGER_H
