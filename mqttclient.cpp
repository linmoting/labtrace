#include "mqttclient.h"


MQTT::MQTT(QObject *parent) : QObject(parent)
{
    client = new QMqttClient(this);
    //设置 MQTT Broker 的地址。
    client->setHostname("broker.hivemq.com");
    //127.0.0.1 表示 EMQX 运行在当前这台电脑,若 EMQX 在另一台电脑，需要改成那台电脑的局域网 IP，例如 "192.168.1.100"。
    //设置 MQTT Broker 的端口。
    client->setPort(1883);//EMQX 默认使用 1883 作为普通 MQTT TCP 连接端口。
    // 设置 MQTT 用户名
    client->setUsername("serve");
    // 设置当前 Qt 服务端作为 MQTT 客户端连接 EMQX 时的唯一名称。
    // 同一台 Broker 中，两个客户端不能使用完全相同的 ID。
    // 例如可以写 "qt_server_001"。
    client->setClientId("qt_server_001");

    //MQTT 连接状态改变时调用onStateChanged函数
    connect(client,
            &QMqttClient::stateChanged,
            this,
            &MQTT::onStateChanged);

    // 当 MQTT 收到消息时，调用 onMessageReceived 函数
    connect(client,
            &QMqttClient::messageReceived,
            this,
            &MQTT::onMessageReceived);
}

void MQTT::start()
{
    // 发起连接请求，让 Qt 服务端真正去连接上面设置的 EMQX 地址和端口。
    // 调用前只是保存配置；调用这一行后才开始联网连接
    client->connectToHost();
}

void MQTT::onStateChanged(QMqttClient::ClientState state)
{
    if (state == QMqttClient::Connected)            //如果连接成功
    {
        qDebug() << "MQTT 已连接 EMQX";
        // 创建 MQTT 订阅主题过滤器
        // + 表示匹配任意一个设备编号
        QMqttTopicFilter topicFilter(
            QStringLiteral("***/program3/device/+/telemetry"));

        // 使用 QoS 1 订阅设备数据
        QMqttSubscription *subscription =
            client->subscribe(topicFilter, 1);
        // 判断订阅是否成功
        if (subscription == nullptr)
        {
            qDebug() << "MQTT 订阅失败";
        }
        else
        {
            qDebug() << "MQTT 订阅成功";
        }
    }
    else if (state == QMqttClient::Disconnected)   //如果连接失败
    {
        qDebug() << "MQTT 已断开";
    }
}
void MQTT::onMessageReceived(const QByteArray &message,
                             const QMqttTopicName &topic)
{
    // 显示收到的主题
    qDebug() << "收到 MQTT 主题:" << topic.name();

    // 显示收到的数据
    qDebug() << "收到 MQTT 数据:" << message;

    // 把消息发送给 Widget 或数据库类
    emit messageReceived(topic.name(), message);
}
bool MQTT::publishCommand(const QString &deviceId,const QByteArray &commandMessage)
{
    // 检查设备编号是否为空
    if (deviceId.isEmpty())
    {
        qDebug() << "发布失败：设备编号为空";
        return false;
    }

    // 检查命令内容是否为空
    if (commandMessage.isEmpty())
    {
        qDebug() << "发布失败：命令内容为空";
        return false;
    }

    // 检查 MQTT 当前是否已经连接
    if (client->state() != QMqttClient::Connected)
    {
        qDebug() << "发布失败：MQTT 尚未连接";
        return false;
    }

    // 为指定设备生成控制命令主题
    // 这里不能使用 + 或 #，发布主题必须是完整主题
    const QString topicText =QStringLiteral("***/program3/device/%1/command")
                            .arg(deviceId);

    // 将字符串主题转换为 MQTT 发布主题对象
    const QMqttTopicName topic(topicText);

    // 使用 QoS 1 发布控制命令
    // 返回 -1 表示发布请求失败
    const qint32 packetId =
        client->publish(
            topic,
            commandMessage,
            1,
            false);

    // 判断发布请求是否成功
    if (packetId == -1)
    {
        qDebug() << "MQTT 控制命令发布失败";
        return false;
    }

    qDebug() << "MQTT 控制命令发布成功:"
             << topicText
             << commandMessage;

    return true;
}
