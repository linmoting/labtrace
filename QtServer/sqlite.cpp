#include "sqlite.h"
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
// 用于解析 JSON
#include <QJsonDocument>
#include <QJsonObject>
// 用于生成当前时间戳
#include <QDateTime>

SQLite::SQLite(QObject *parent)
    : QObject(parent)
{

}

bool SQLite::open(const QString &filePath)
{
    //给数据库连接设置唯一名称
    const QString connectionName = QStringLiteral("server_database");
    // 如果连接已经存在，就继续使用
    if (QSqlDatabase::contains(connectionName))
    {
        database = QSqlDatabase::database(connectionName);
    }
    else
    {
        // 使用 Qt 的 SQLite 驱动创建数据库连接
        database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"),
            connectionName);
    }
    // 设置数据库文件路径
    // 如果文件不存在，SQLite 会自动创建它
    database.setDatabaseName(filePath);//D:\QT\qt document\lianxi/server/SQLite1.s3db
    // 打开数据库
    if (!database.open())
    {
        qDebug() << "SQLite 打开失败:"
                 << database.lastError().text();

        return false;
    }
    // 输出数据库打开成功
    qDebug() << "SQLite 已打开:"
             << filePath;

    // 创建数据表
    return createTables();
}

bool SQLite::createTables()
{
    //使用当前数据库连接创建SQL查询对象
    QSqlQuery query(database);

    //创建数据表 用于设备采集
    const QString sql = QStringLiteral(
            "CREATE TABLE IF NOT EXISTS telemetry ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  record_type TEXT NOT NULL DEFAULT 'usage',"
            "  device_id TEXT NOT NULL,"
            "  seq INTEGER NOT NULL,"
            "  timestamp INTEGER NOT NULL,"
            "  operator_name TEXT,"
            "  operator_uid TEXT,"
            "  reagent_name TEXT,"
            "  reagent_uid TEXT,"
            "  weight_before REAL,"
            "  weight_after REAL,"
            "  weight_delta REAL,"
            "  temperature REAL,"
            "  humidity REAL,"
            "  distance_cm INTEGER,"
            "  bottle_present INTEGER,"
            "  received_at TEXT NOT NULL,"
            "  UNIQUE(device_id, seq, timestamp)"
            ")");

    // 执行建表语句
    if (!query.exec(sql))
    {
        qDebug() << "创建 telemetry 表失败:"
                 << query.lastError().text();

        return false;
    }

    qDebug() << "telemetry 数据表准备完成";

    return true;
}

bool SQLite::insertTelemetry(const QByteArray &message)
{
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(message, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qDebug() << "JSON 解析失败";
        return false;
    }

    QJsonObject object = document.object();

    QString deviceId = object.value("device_id").toString();
    int     seq      = object.value("seq").toInt(-1);

    if (deviceId.isEmpty() || seq < 0) {
        qDebug() << "JSON 缺少 device_id 或 seq";
        return false;
    }

    // timestamp：可能是字符串，也可能是毫秒
    qint64 timestamp = object.value("timestamp").toVariant().toLongLong();
    if (timestamp <= 0 && object.value("timestamp").isString()) {
        QDateTime dt = QDateTime::fromString(
            object.value("timestamp").toString(),
            QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (dt.isValid())
            timestamp = dt.toMSecsSinceEpoch();
    }
    if (timestamp <= 0)
        timestamp = QDateTime::currentMSecsSinceEpoch();

    QString recordType   = object.value("type").toString(QStringLiteral("usage"));
    QString operatorName = object.value("operator_name").toString();
    QString operatorUid  = object.value("operator_uid").toString();
    QString reagentName  = object.value("reagent_name").toString();
    QString reagentUid   = object.value("reagent_uid").toString();
    double  weightBefore = object.value("weight_before").toDouble();
    double  weightAfter  = object.value("weight_after").toDouble();
    double  weightDelta  = object.value("weight_delta").toDouble();
    double  temperature  = object.value("temperature").toDouble();
    double  humidity     = object.value("humidity").toDouble();
    int     distanceCm   = object.value("distance_cm").toInt();
    int     bottlePresent= object.value("bottle_present").toBool()
                            ? 1 : object.value("bottle_present").toInt();
    QString receivedAt   = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO telemetry "
        "(record_type, device_id, seq, timestamp, "
        " operator_name, operator_uid, reagent_name, reagent_uid, "
        " weight_before, weight_after, weight_delta, "
        " temperature, humidity, distance_cm, bottle_present, received_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));

    query.addBindValue(recordType);
    query.addBindValue(deviceId);
    query.addBindValue(seq);
    query.addBindValue(timestamp);
    query.addBindValue(operatorName);
    query.addBindValue(operatorUid);
    query.addBindValue(reagentName);
    query.addBindValue(reagentUid);
    query.addBindValue(weightBefore);
    query.addBindValue(weightAfter);
    query.addBindValue(weightDelta);
    query.addBindValue(temperature);
    query.addBindValue(humidity);
    query.addBindValue(distanceCm);
    query.addBindValue(bottlePresent);
    query.addBindValue(receivedAt);

    if (!query.exec()) {
        qDebug() << "写入 telemetry 失败:" << query.lastError().text();
        return false;
    }

    if (query.numRowsAffected() == 0)
        qDebug() << "重复数据，已忽略:" << deviceId << seq;
    else
        qDebug() << "数据写入成功:" << deviceId << seq;

    return true;
}

bool SQLite::queryTelemetry(const QString &deviceId,
                            qint64 startTime,
                            qint64 endTime,
                            QJsonArray &result)
{
    // 清空上一次查询结果
    result = QJsonArray();

    // 确认数据库已经打开
    if (!database.isOpen())
    {
        qDebug() << "历史查询失败：数据库未打开";
        return false;
    }

    QSqlQuery query(database);

    // 查询指定设备在时间范围内的数据
    query.prepare(QStringLiteral(
            "SELECT record_type, device_id, seq, timestamp, "
            "       operator_name, operator_uid, reagent_name, reagent_uid, "
            "       weight_before, weight_after, weight_delta, "
            "       temperature, humidity, distance_cm, bottle_present, received_at "
            "FROM telemetry "
            "WHERE device_id = ? AND timestamp >= ? AND timestamp <= ? "
            "ORDER BY timestamp ASC"));

    // 绑定查询条件
    query.addBindValue(deviceId);
    query.addBindValue(startTime);
    query.addBindValue(endTime);

    if (!query.exec())
    {
        qDebug() << "历史数据查询失败:"
                 << query.lastError().text();
        return false;
    }

    // 将每一行数据库记录转换为 JSON
    while (query.next())
    {
        QJsonObject row;

        row.insert("type",           query.value(0).toString());
        row.insert("device_id",      query.value(1).toString());
        row.insert("seq",            query.value(2).toInt());
        row.insert("timestamp",      query.value(3).toLongLong());
        row.insert("operator_name",  query.value(4).toString());
        row.insert("operator_uid",   query.value(5).toString());
        row.insert("reagent_name",   query.value(6).toString());
        row.insert("reagent_uid",    query.value(7).toString());
        row.insert("weight_before",  query.value(8).toDouble());
        row.insert("weight_after",   query.value(9).toDouble());
        row.insert("weight_delta",   query.value(10).toDouble());
        row.insert("temperature",    query.value(11).toDouble());
        row.insert("humidity",       query.value(12).toDouble());
        row.insert("distance_cm",    query.value(13).toInt());
        row.insert("bottle_present", query.value(14).toInt() != 0);
        row.insert("received_at",    query.value(15).toString());
        result.append(row);
    }

    qDebug() << "历史数据查询完成，共"
             << result.size()
             << "条";

    return true;
}
