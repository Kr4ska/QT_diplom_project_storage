#include "yandexapimanager.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>

YandexApiManager::YandexApiManager(QObject *parent)
    : QObject(parent), m_networkManager(new QNetworkAccessManager(this))
{
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &YandexApiManager::onReplyFinished);
}

void YandexApiManager::sendRequest(const QString &prompt, const QString &apiKey, const QString &folderId, const QString &modelUri, double temperature)
{
    QUrl url("https://ai.api.cloud.yandex.net/v1/responses");
    QNetworkRequest request(url);

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QString authHeader = "Api-Key " + apiKey;
    request.setRawHeader("Authorization", authHeader.toUtf8());

    QJsonObject requestBody;
    requestBody["model"] = modelUri.isEmpty() ? "gpt://" + folderId + "/yandexgpt-lite" : modelUri;
    requestBody["temperature"] = temperature;
    requestBody["max_output_tokens"] = 1500;

    // We send plain text input as required by YandexGPT (though the user might use Completion API format)
    requestBody["input"] = prompt;

    // Note: If using Chat API instead of completion, the body structure would be:
    // requestBody["modelUri"] = "gpt://" + folderId + "/yandexgpt-lite";
    // QJsonObject message; message["role"] = "user"; message["text"] = prompt;
    // QJsonArray messages; messages.append(message);
    // requestBody["messages"] = messages;

    QJsonDocument doc(requestBody);
    QByteArray data = doc.toJson();

    m_networkManager->post(request, data);
}

void YandexApiManager::onReplyFinished(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(reply->errorString());
        reply->deleteLater();
        return;
    }

    QByteArray responseData = reply->readAll();
    QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);

    if (jsonDoc.isNull() || !jsonDoc.isObject()) {
        emit errorOccurred("Invalid JSON response from server");
        reply->deleteLater();
        return;
    }

    QJsonObject rootObj = jsonDoc.object();
    QString resultText = "";

    // The provided API example checks multiple fields
    if (rootObj.contains("output") && rootObj["output"].isArray() && !rootObj["output"].toArray().isEmpty()) {
        QJsonObject firstOut = rootObj["output"].toArray()[0].toObject();
        if (firstOut.contains("content") && firstOut["content"].isArray() && !firstOut["content"].toArray().isEmpty()) {
             resultText = firstOut["content"].toArray()[0].toObject()["text"].toString();
        }
    } else if (rootObj.contains("result") && rootObj["result"].isObject()) {
        QJsonObject resultObj = rootObj["result"].toObject();
        if (resultObj.contains("alternatives") && resultObj["alternatives"].isArray() && !resultObj["alternatives"].toArray().isEmpty()) {
            QJsonObject firstAlt = resultObj["alternatives"].toArray()[0].toObject();
            if (firstAlt.contains("message") && firstAlt["message"].isObject()) {
                resultText = firstAlt["message"].toObject()["text"].toString();
            }
        }
    }

    if (!resultText.isEmpty()) {
        emit responseReceived(resultText);
    } else {
        // Fallback: Dump the whole JSON string so we can debug what the server actually returned
        emit responseReceived(jsonDoc.toJson(QJsonDocument::Indented));
    }

    reply->deleteLater();
}
