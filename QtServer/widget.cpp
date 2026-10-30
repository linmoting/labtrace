#include "widget.h"
#include "ui_widget.h"
#include <QTimer>
#include <QMessageBox>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    qDebug() << "SSL 支持:" << QSslSocket::supportsSsl();
    ui->setupUi(this);

    m_netMgr = new QNetworkAccessManager(this);
    ui->ipEdit->setText(getLocalIP());   // 默认值
    // Web 服务
    webServer = new WebServer(this);
    if(!webServer->start(8080, 8081))
    {
        ui->textBrowser->append("Web 服务启动失败（端口可能被占用）");
    }
    else
    {
        ui->textBrowser->append("Web 服务已启动：HTTP 8081 / WS 8080");
    }

    // 创建 SQLite 管理对象
    sqlite = new SQLite(this);
    QString dbPath = QCoreApplication::applicationDirPath() + "/server_data.db";

    // 打开数据库并创建 telemetry 表
    if (sqlite->open(dbPath))
    {
        ui->textBrowser->append("SQLite 数据库初始化成功");
    }
    else
    {
        ui->textBrowser->append("SQLite 数据库初始化失败");
    }

    // 创建 TCP 服务对象
    tcpServer = new TcpServer(this);

    // 连接 TCP 状态信号
    connect(tcpServer,
            &TcpServer::clientConnected,
            this,
            &Widget::onTcpClientConnected);

    connect(tcpServer,
            &TcpServer::clientDisconnected,
            this,
            &Widget::onTcpClientDisconnected);

    connect(tcpServer,
            &TcpServer::messageReceived,
            this,
            &Widget::onTcpMessageReceived);

    // 启动 TCP 服务，监听 6666 端口
    if (!tcpServer->start(6666))
    {
        ui->textBrowser->append("TCP 服务启动失败");
    }
    else
    {
        ui->textBrowser->append("TCP 服务已启动，端口 6666");
    }

    mqtt = new MQTT(this);
    // 将 MQTT 类的消息信号连接到 Widget 的显示槽函数
    connect(mqtt,
            &MQTT::messageReceived,
            this,
            &Widget::onMqttMessageReceived);
    mqtt->start();
}

Widget::~Widget()
{
    delete ui;
}

void Widget::on_pushButton_clicked()
{
    QString input = ui->lineEdit->text().trimmed();
        if(input.isEmpty())
        {
            QMessageBox::warning(this, "提示", "请输入内容");
            return;
        }

        // 解析 JSON
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(input.toUtf8(), &err);

        if(err.error != QJsonParseError::NoError || !doc.isObject())
        {
            QMessageBox::warning(this, "格式错误",
                "不是有效 JSON: " + err.errorString());
            return;
        }

        QJsonObject obj = doc.object();
        QString type = obj.value("type").toString();

        if(type == "command")
        {
            // MQTT 发给设备
            QString devId = obj.value("device_id").toString("stm32_001");
            obj.remove("type");
            obj.remove("device_id");
            mqtt->publishCommand(devId,QJsonDocument(obj).toJson(QJsonDocument::Compact));
            ui->textBrowser->append("[MQTT] 命令已发 -> " + devId);
        }
        else
        {
            // 广播给客户端（如果有 WebServer 也广播）
            QByteArray msg = QJsonDocument(obj).toJson(QJsonDocument::Compact);
            msg.append('\n');
            tcpServer->sendMessage(msg);
            // webServer->broadcast(msg);   // 如果加了 WebServer
            ui->textBrowser->append("[广播] type=" + type);
        }
}
void Widget::onTcpClientConnected()
{
    // 在界面显示客户端连接状态
    ui->textBrowser->append(
        "TCP 客户端已连接");
}

void Widget::onTcpClientDisconnected()
{
    // 在界面显示客户端断开状态
    ui->textBrowser->append("TCP 客户端已断开");
}

void Widget::onTcpMessageReceived(const QByteArray &message)
{
    if(message.size() > 1024 * 1024)
    {   // 1MB
        ui->textBrowser->append("消息超长，丢弃");
        return;
    }

    // 显示客户端原始数据
    ui->textBrowser->append("客户端: " +QString::fromUtf8(message));

    // 解析客户端发送的 JSON
    QJsonParseError parseError;

    const QJsonDocument document =QJsonDocument::fromJson(message.trimmed(),&parseError);

    // 检查 JSON 格式是否正确
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        ui->textBrowser->append("控制命令格式错误：不是有效 JSON");
        return;
    }

    // 获取 JSON 对象
    const QJsonObject request = document.object();

    // 获取消息类型
    const QString type = request.value("type").toString();

    // 处理历史数据查询请求
    if (type == QStringLiteral("history_query"))
    {
        // 获取客户端传来的查询条件
        const QString deviceId =
            request.value("device_id").toString();

        const qint64 startTime =
            request.value("start_time")
                   .toVariant()
                   .toLongLong();

        const qint64 endTime =
            request.value("end_time")
                   .toVariant()
                   .toLongLong();

        // 检查设备编号是否填写
        if (deviceId.isEmpty())
        {
            ui->textBrowser->append("历史查询失败：缺少 device_id");

            return;
        }

        // 用来保存数据库查询结果
        QJsonArray historyData;

        // 调用 SQLite 查询函数
        const bool success =sqlite->queryTelemetry(
                                        deviceId,
                                        startTime,
                                        endTime,
                                        historyData);

        // 构造返回给客户端的 JSON
        QJsonObject response;

        response.insert("type",QStringLiteral("history_result"));

        response.insert("success",success);

        response.insert("device_id",deviceId);

        response.insert("data",historyData);

        // 转换为一行 JSON
        QByteArray responseMessage =
            QJsonDocument(response)
                .toJson(QJsonDocument::Compact);

        // 一行表示一条完整 TCP 消息
        responseMessage.append('\n');

        // 将历史查询结果返回客户端
        tcpServer->sendMessage(responseMessage);

        ui->textBrowser->append(QString("历史查询完成，共返回 %1 条数据")
                         .arg(historyData.size()));

        // 历史查询已经处理完，不再继续处理控制命令
        return;
    }

    // 不是历史查询，再检查是否为控制命令
    if (type != QStringLiteral("command"))
    {
        ui->textBrowser->append("无法识别的消息类型: " + type);

        return;
    }

    // 获取设备编号
    const QString deviceId =request.value("device_id").toString();

    // 检查设备编号
    if (deviceId.isEmpty())
    {
        ui->textBrowser->append("控制命令缺少 device_id");

        return;
    }

    // 复制一份命令对象
    QJsonObject commandObject = request;

    // 将设备命令转换为 JSON 字节数据
    const QByteArray commandMessage =
        QJsonDocument(commandObject)
            .toJson(QJsonDocument::Compact);

    // 通过 MQTT 发布到指定设备
    const bool success =mqtt->publishCommand(deviceId,commandMessage);

    // 把 MQTT 发布结果显示在服务端界面
    if (success)
    {
        ui->textBrowser->append("MQTT 控制命令发布成功");
    }
    else
    {
        ui->textBrowser->append("MQTT 控制命令发布失败");
    }

    // 给 TCP 客户端返回处理结果
    QJsonObject result;

    result.insert("type",QStringLiteral("command_result"));

    result.insert("device_id",deviceId);

    result.insert("success",success);

    result.insert("message",success? QStringLiteral("命令已发布到 MQTT")
                                   : QStringLiteral("命令发布失败"));

    // 加换行符，表示一条 TCP 消息结束
    QByteArray resultMessage =QJsonDocument(result).toJson(QJsonDocument::Compact);

    resultMessage.append('\n');

    // 将结果返回给 Qt 客户端
    tcpServer->sendMessage(resultMessage);
    webServer->broadcast(resultMessage);

}

void Widget::onMqttMessageReceived(const QString &topic, const QByteArray &message)
{
    //  ① 显示原始
    ui->textBrowser->append("MQTT主题: " + topic);
    ui->textBrowser->append("MQTT数据: " + QString::fromUtf8(message));

    //  ② 解析 JSON
    QJsonParseError parseError;
    QJsonDocument dataDocument = QJsonDocument::fromJson(message, &parseError);

    if (parseError.error != QJsonParseError::NoError || !dataDocument.isObject())
    {
        ui->textBrowser->append("MQTT 数据不是有效 JSON，已丢弃");
        return;
    }

    QJsonObject data = dataDocument.object();

    //  ③ 判断 device_id
    QString deviceId = data.value("device_id").toString();
    if (deviceId.isEmpty())
    {
        ui->textBrowser->append("⚠️ 跳过：缺 device_id");
        return;
    }

    //  ④ 判断"是数据还是命令"
    if (data.contains("cmd"))
    {
        ui->textBrowser->append("⚠️ 跳过：这是控制命令，不是数据");
        return;
    }

    //  ⑤ 时间戳规范化（字符串 → 毫秒）
    if (data.value("timestamp").isString())
    {
        const QDateTime deviceTime = QDateTime::fromString(
            data.value("timestamp").toString(),
            QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (deviceTime.isValid())
            data.insert("timestamp", deviceTime.toMSecsSinceEpoch());
    }

    //  ⑥ 规范化后再取 ts
    qint64 ts = data.value("timestamp").toVariant().toLongLong();
    if (ts <= 0)
    {
        ui->textBrowser->append("⚠️ 跳过：timestamp 无效");
        return;
    }

    //  ⑦ 名字兜底
    if (data.value("operator_name").toString().isEmpty())
        data.insert("operator_name", data.value("operator_uid").toString());
    if (data.value("reagent_name").toString().isEmpty())
        data.insert("reagent_name", data.value("reagent_uid").toString());

    //  ⑧ 规范化后的 JSON
    QByteArray normalized = QJsonDocument(data).toJson(QJsonDocument::Compact);

    //  ⑨ 去重
    QString key = QString("%1_%2").arg(deviceId).arg(ts);

    if (m_sentKeys.contains(key))
    {
        ui->textBrowser->append("⚠️ 重复数据，跳过转发: " + key);
        return;
    }
    m_sentKeys.insert(key);

    if (m_sentKeys.size() > 5000)
    {
        m_sentKeys.clear();
        ui->textBrowser->append("[维护] 去重缓存已清空");
    }

    //  ⑩ 入库（用规范化后的 JSON）
    if (!sqlite->insertTelemetry(normalized))
        ui->textBrowser->append("SQLite 写入失败");
    else
        ui->textBrowser->append("SQLite 写入成功");

    //  ⑪ 转发
    QJsonObject packet;
    packet.insert("type",  QStringLiteral("telemetry"));
    packet.insert("topic", topic);
    packet.insert("data",  data);

    QByteArray tcpMessage = QJsonDocument(packet).toJson(QJsonDocument::Compact);
    tcpMessage.append('\n');

    tcpServer->sendMessage(tcpMessage);
    webServer->broadcast(tcpMessage);

    ui->textBrowser->append("实时数据已转发给 TCP 客户端+ Web 客户端");
}

void Widget::on_sendbtn_clicked()
{
    QJsonObject data;
    data["device_id"]      = "stm32_001";
    data["seq"]            = 1;
    data["type"]           = "usage";
    data["operator_name"]  = "张三";
    data["operator_uid"]   = "A1B2C3D4";
    data["reagent_name"]   = "1号试剂";
    data["reagent_uid"]    = "B2C3D4E5";
    data["weight_before"]  = 125.5;
    data["weight_after"]   = 120.5;
    data["weight_delta"]   = 5.0;
    data["temperature"]    = 25.6;
    data["humidity"]       = 48.2;
    data["distance_cm"]    = 30;
    data["bottle_present"] = 1;
    data["timestamp"]      = QDateTime::currentMSecsSinceEpoch();

    // 模仿入库
    QByteArray dataJson = QJsonDocument(data).toJson(QJsonDocument::Compact);
    if (sqlite->insertTelemetry(dataJson))
        ui->textBrowser->append("SQLite 写入成功");
    else
        ui->textBrowser->append("SQLite 写入失败");

    // 转发
    QJsonObject packet;
    packet["type"]  = "telemetry";
    packet["topic"] = "device/stm32_001/data";
    packet["data"]  = data;

    QByteArray msg = QJsonDocument(packet).toJson(QJsonDocument::Compact);
    msg.append('\n');

    tcpServer->sendMessage(msg);
    webServer->broadcast(msg);
    ui->textBrowser->append("[测试] 已发送伪造 telemetry 数据");
}

QString Widget::getLocalIP()
{
    for(const QNetworkInterface &iface : QNetworkInterface::allInterfaces())
    {
        // 跳过回环、未启用
        if(iface.flags() & QNetworkInterface::IsLoopBack) continue;
        if(!(iface.flags() & QNetworkInterface::IsUp)) continue;

        // 跳过虚拟网卡
        QString name = iface.humanReadableName();
        if(name.contains("VMware", Qt::CaseInsensitive) ||
           name.contains("VirtualBox", Qt::CaseInsensitive) ||
           name.contains("Hyper-V", Qt::CaseInsensitive))
        {
            continue;
        }

        // 取该网卡的 IPv4
        for(const QNetworkAddressEntry &entry : iface.addressEntries())
        {
            QHostAddress addr = entry.ip();
            if(addr.protocol() == QAbstractSocket::IPv4Protocol
               && !addr.toString().startsWith("169.254"))
            {
                return addr.toString();
            }
        }
    }
    return "127.0.0.1";
}

void Widget::on_genQrBtn_clicked()
{
    QString ip = ui->ipEdit->text().trimmed();
    // 清理用户可能的输入
        ip.remove("http://");
        ip.remove("https://");

    // 如果输入框为空，自动获取
    if(ip.isEmpty())
    {
        ip = getLocalIP();
        ui->ipEdit->setText(ip);
    }

    if(ip.isEmpty())
    {
        QMessageBox::warning(this, "提示", "请输入 IP");
        return;
    }

    QString url = QString("http://%1:8081").arg(ip);

    // 拼 API URL
    QString apiUrl = QString(
        "http://api.qrserver.com/v1/create-qr-code/?size=360x360&data=%1")
        .arg(QString::fromUtf8(QUrl::toPercentEncoding(url)));

    QNetworkRequest req{QUrl(apiUrl)};
    QNetworkReply *reply = m_netMgr->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply]
    {
        onQRReplyFinished(reply);
    });

    ui->textBrowser->append("📱 请求二维码: " + url);
}

void Widget::onQRReplyFinished(QNetworkReply *reply)
{
    if(reply->error() != QNetworkReply::NoError)
    {
        ui->textBrowser->append("⚠️ 二维码下载失败: " + reply->errorString());
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QPixmap pix;
    if(!pix.loadFromData(data))
    {
        ui->textBrowser->append("⚠️ 二维码图片解析失败");
        return;
    }

    // 弹出 Dialog 显示二维码
    QRCodeDialog dlg(this);
    dlg.setQRPixmap(pix);
    dlg.setHintText("手机扫码访问");

    ui->textBrowser->append("✅ 二维码已生成");
    dlg.exec();     // 模态：阻塞主界面，用户点"关闭"才返回
}
