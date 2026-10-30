#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QDebug>
#include "mqttclient.h"
#include "sqlite.h"
#include "tcpserver.h"
#include "webserver.h"
#include "qrcodedialog.h"
// 用于构造和解析 JSON
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
//访问网页端
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
//ip地址
#include <QNetworkInterface>
#include <QHostAddress>
//转发前数据去重
#include <QSet>
QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    void on_pushButton_clicked();

    // TCP 客户端连接时调用
    void onTcpClientConnected();

    // TCP 客户端断开时调用
    void onTcpClientDisconnected();

    // 收到 TCP 客户端数据时调用
    void onTcpMessageReceived(const QByteArray &message);

    // 收到 MQTT 消息后，在界面中显示
    void onMqttMessageReceived(const QString &topic,const QByteArray &message);

    void on_sendbtn_clicked();

    void on_genQrBtn_clicked();

    void onQRReplyFinished(QNetworkReply *reply);

    //获取当前ip
    QString getLocalIP();

private:
    Ui::Widget *ui;
    TcpServer *tcpServer;
    MQTT *mqtt;
    SQLite *sqlite;
    WebServer *webServer;
    QRCodeDialog *m_qrDialog = nullptr;   // ← 弹窗成员
    QNetworkAccessManager *m_netMgr = nullptr;
    QSet<QString> m_sentKeys;   // 已转发的 key
};
#endif // WIDGET_H
