#ifndef SQLITE_H
#define SQLITE_H

#include <QObject>
#include <QSqlDatabase>
#include <QByteArray>
#include <QJsonArray>

class SQLite : public QObject
{
    Q_OBJECT
public:
    explicit SQLite(QObject *parent = nullptr);

    // 打开数据库文件，并创建数据表
    bool open(const QString &filePath);
    // 将 MQTT 收到的 JSON 数据写入 telemetry 表
    bool insertTelemetry(const QByteArray &message);
    // 按设备编号和时间范围查询历史数据
    bool queryTelemetry(const QString &deviceId,
                        qint64 startTime,
                        qint64 endTime,
                        QJsonArray &result);

private:
    //创建telemetry数据表
    bool createTables();

    //当前数据库连接对象
     QSqlDatabase database;
};

#endif // SQLITE_H
