#include "tcpserver.h"

TcpServer::TcpServer(QObject *parent)
    : QObject(parent)
{
    // 创建 TCP 服务器对象
    server = new QTcpServer(this);

    // 初始时还没有客户端连接
    socket = nullptr;

    // 服务器收到新连接时，调用 onNewConnection()
    connect(server,
            &QTcpServer::newConnection,
            this,
            &TcpServer::onNewConnection);
}
bool TcpServer::start(quint16 port)
{
    // 在所有网卡上监听指定端口
    if (!server->listen(QHostAddress::Any, port))
    {
        qDebug() << "TCP 服务器启动失败:"
                 << server->errorString();

        return false;
    }

    qDebug() << "TCP 服务器已启动，端口:"
             << port;

    return true;
}
void TcpServer::onNewConnection()
{
    // 如果已有客户端，先取出新的连接
    if (socket != nullptr)
    {
        QTcpSocket *newSocket =
            server->nextPendingConnection();

        qDebug() << "当前只支持一个客户端，拒绝新连接";

        newSocket->disconnectFromHost();
        newSocket->deleteLater();

        return;
    }
    // 获取等待连接的客户端
    socket = server->nextPendingConnection();

    if (socket == nullptr)
    {
        qDebug() << "获取客户端连接失败";
        return;
    }

    // 客户端有数据到达时，调用 onReadyRead()
    connect(socket,
            &QTcpSocket::readyRead,
            this,
            &TcpServer::onReadyRead);

    // 客户端断开时，调用 onDisconnected()
    connect(socket,
            &QTcpSocket::disconnected,
            this,
            &TcpServer::onDisconnected);

    qDebug() << "TCP 客户端已连接:"
             << socket->peerAddress().toString();

    // 通知 Widget
    emit clientConnected();
}
void TcpServer::onReadyRead()
{
    if (socket == nullptr)
    {
        return;
    }

    // 读取客户端发来的数据
    const QByteArray message = socket->readAll();

    // 把数据交给 Widget
    emit messageReceived(message);
}
void TcpServer::onDisconnected()
{
    qDebug() << "TCP 客户端已断开";

    // 延迟销毁 socket 对象
    if (socket != nullptr)
    {
        socket->deleteLater();
        socket = nullptr;
    }

    // 通知 Widget
    emit clientDisconnected();
}
void TcpServer::sendMessage(const QByteArray &message)
{
    // 没有客户端时不能发送
    if (socket == nullptr)
    {
        qDebug() << "没有 TCP 客户端，无法发送";
        return;
    }

    // 检查客户端是否仍然连接
    if (socket->state() != QAbstractSocket::ConnectedState)
    {
        qDebug() << "TCP 客户端未连接";
        return;
    }

    // 向客户端发送数据
    socket->write(message);

    // 立即刷新发送缓冲区
    socket->flush();
}
