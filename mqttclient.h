#ifndef MQTT_H
#define MQTT_H
//连接 EMQX
//订阅设备数据主题
//接收 MQTT 消息
//发布控制命令
//自动重连
//通过信号把数据交给 Widget 或数据库模块

#include <QObject>
#include <QMqttClient>
#include <QDebug>

class MQTT: public QObject
{
    Q_OBJECT
public:
    explicit MQTT(QObject *parent = nullptr);
    void start();//开始连接 EMQX
    // 向指定设备发布控制命令
    bool publishCommand(const QString &deviceId,const QByteArray &commandMessage);

signals:
    // 把收到的主题和消息发送给 Widget 或数据库类
    void messageReceived(const QString &topic,
                         const QByteArray &message);
private slots:
    // MQTT 连接状态改变时调用
    void onStateChanged(QMqttClient::ClientState state);
    // 收到 MQTT 消息时调用
    void onMessageReceived(const QByteArray &message,
                           const QMqttTopicName &topic);
private:
    QMqttClient *client; // Qt MQTT 客户端对象

};

#endif // MQTT_H
