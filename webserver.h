#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <QObject>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QList>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonParseError>

class WebServer : public QObject
{
    Q_OBJECT
public:
    explicit WebServer(QObject *parent = nullptr);
    ~WebServer();

    // 启动 WebSocket + HTTP 双服务
    // wsPort   默认 8080（手机实时数据）
    // httpPort 默认 8081（手机页面）
    bool start(quint16 wsPort = 8080, quint16 httpPort = 8081);

    // 给所有已连接的 Web 客户端广播一条消息
    // 参数是带 \n 的 TCP 格式（会自动剥掉 \n）
    void broadcast(const QByteArray &jsonWithNewline);

signals:
    // 从 Web 客户端收到命令（手机端发来的查询/控制）
    void webCommandReceived(const QJsonObject &cmd);

private slots:
    void onNewConnection();
    void onTextMessageReceived(const QString &msg);
    void onDisconnected();

    // HTTP 部分
    void onHttpNewConnection();

private:
    QWebSocketServer *m_wsServer;
    QList<QWebSocket*> m_clients;

    QTcpServer *m_httpServer;

    QList<QByteArray> m_recentMessages;   // 最近 50 条
};

#endif // WEBSERVER_H
