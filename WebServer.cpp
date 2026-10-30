#include "webserver.h"
#include <QDebug>
#include <QFile>

//  构造函数：创建 WebSocket 服务和 HTTP 服务对象
WebServer::WebServer(QObject *parent) : QObject(parent)
{
    //  WebSocket 服务
    m_wsServer = new QWebSocketServer(
        QStringLiteral("ReagentWebServer"),
        QWebSocketServer::NonSecureMode,
        this);

    connect(m_wsServer, &QWebSocketServer::newConnection,
            this, &WebServer::onNewConnection);

    //  HTTP 服务（用于给手机下发 index.html）
    m_httpServer = new QTcpServer(this);
    connect(m_httpServer, &QTcpServer::newConnection,
            this, &WebServer::onHttpNewConnection);
}

//  析构：关闭所有服务、清理所有客户端
WebServer::~WebServer()
{
    if(m_wsServer) m_wsServer->close();
    if(m_httpServer) m_httpServer->close();

    for(auto *c : m_clients)
    {
        c->close();
        c->deleteLater();
    }
    m_clients.clear();
}

//  启动两个服务
bool WebServer::start(quint16 wsPort, quint16 httpPort)
{
    //  WebSocket
    if(!m_wsServer->listen(QHostAddress::Any, wsPort))
    {
        qDebug() << "WebSocket 服务启动失败:" << m_wsServer->errorString();
        return false;
    }
    qDebug() << "WebSocket 服务已启动，端口:" << wsPort;

    //  HTTP
    if(!m_httpServer->listen(QHostAddress::Any, httpPort))
    {
        qDebug() << "HTTP 服务启动失败:" << m_httpServer->errorString();
        return false;
    }
    qDebug() << "HTTP 服务已启动，端口:" << httpPort;

    return true;
}

//  WebSocket：新连接
void WebServer::onNewConnection()
{
    QWebSocket *client = m_wsServer->nextPendingConnection();
    m_clients.append(client);

    connect(client, &QWebSocket::textMessageReceived,
            this, &WebServer::onTextMessageReceived);
    connect(client, &QWebSocket::disconnected,
            this, &WebServer::onDisconnected);

    qDebug() << "Web 客户端已连接，当前在线:" << m_clients.size();

    // 连上后先推一个欢迎消息
    QJsonObject hello;
    hello["type"] = "welcome";
    hello["message"] = "已连接到试剂追溯系统";
    client->sendTextMessage(
        QString::fromUtf8(QJsonDocument(hello).toJson(QJsonDocument::Compact)));

    // 新客户端连上时，先推缓存
    for(const QByteArray &msg : m_recentMessages)
    {
        client->sendTextMessage(QString::fromUtf8(msg).trimmed());
        qDebug() << "推缓存:" << QString::fromUtf8(msg).left(80);   //  加日志
    }
}

//  WebSocket：收到手机发来的消息
void WebServer::onTextMessageReceived(const QString &msg)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(msg.toUtf8(), &err);
    if(err.error != QJsonParseError::NoError || !doc.isObject())
    {
        qDebug() << "Web 消息不是合法 JSON:" << msg;
        return;
    }
    emit webCommandReceived(doc.object());
}


//  WebSocket：客户端断开
void WebServer::onDisconnected()
{
    QWebSocket *client = qobject_cast<QWebSocket*>(sender());
    if(client)
    {
        m_clients.removeAll(client);
        client->deleteLater();
        qDebug() << "Web 客户端断开，当前在线:" << m_clients.size();
    }
}


//  广播：给所有手机推一条消息
void WebServer::broadcast(const QByteArray &jsonWithNewline)
{
    // 存入缓存
    m_recentMessages.append(jsonWithNewline);
    if(m_recentMessages.size() > 50)
        m_recentMessages.removeFirst();

    if(m_clients.isEmpty()) return;

    // 去掉末尾的 \n 和 \r（WebSocket 一条消息就是一帧，不需要换行符）
    QByteArray data = jsonWithNewline;
    while(data.endsWith('\n') || data.endsWith('\r'))
        data.chop(1);

    const QString text = QString::fromUtf8(data);

    for(auto *c : m_clients) {
        if(c->state() == QAbstractSocket::ConnectedState)
            c->sendTextMessage(text);
    }
}

//  HTTP：新连接 → 返回 index.html
void WebServer::onHttpNewConnection()
{
    QTcpSocket *s = m_httpServer->nextPendingConnection();
    if(!s) return;

    connect(s, &QTcpSocket::readyRead, this, [this, s]
    {
        // 忽略 HTTP 请求内容，固定返回 index.html
        s->readAll();

        QFile f(":/web/index.html");
        if(!f.open(QIODevice::ReadOnly))
        {
            qDebug() << "❌ 读不到 :/web/index.html，检查 qrc 配置";
            s->write("HTTP/1.1 404 Not Found\r\n"
                     "Content-Length: 0\r\n"
                     "Connection: close\r\n\r\n");
            s->disconnectFromHost();
            return;
        }

        QByteArray body = f.readAll();
        f.close();

        QByteArray resp;
        resp += "HTTP/1.1 200 OK\r\n";
        resp += "Content-Type: text/html; charset=utf-8\r\n";
        resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        resp += "Connection: close\r\n";
        resp += "\r\n";
        resp += body;

        s->write(resp);
        s->flush();
        s->disconnectFromHost();
    });

    // 客户端断开后清理
    connect(s, &QTcpSocket::disconnected, s, &QTcpSocket::deleteLater);
}
