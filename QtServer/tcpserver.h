#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDebug>

class TcpServer : public QObject
{
    Q_OBJECT
public:
    explicit TcpServer(QObject *parent = nullptr);

    // 开始监听指定端口
    bool start(quint16 port);

    // 向当前客户端发送数据
    void sendMessage(const QByteArray &message);

signals:
    // 有客户端连接时发出
    void clientConnected();

    // 客户端断开时发出
    void clientDisconnected();

    // 收到客户端数据时发出
    void messageReceived(const QByteArray &message);


private slots:
    // 处理新客户端连接
    void onNewConnection();

    // 处理客户端发送的数据
    void onReadyRead();

    // 处理客户端断开
    void onDisconnected();

private:
    // TCP 服务器对象
    QTcpServer *server;

    // 当前连接的客户端
    // 目前先支持一个客户端
    QTcpSocket *socket;
};

#endif
