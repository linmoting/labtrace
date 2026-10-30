#ifndef QRCODEDIALOG_H
#define QRCODEDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

class QRCodeDialog : public QDialog
{
    Q_OBJECT
public:
    explicit QRCodeDialog(QWidget *parent = nullptr);

    // 显示二维码图片
    void setQRPixmap(const QPixmap &pix);

    // 设置提示文字
    void setHintText(const QString &text);

private slots:
    void onCloseBtnClicked();

private:
    QLabel      *m_hintLabel;
    QLabel      *m_qrLabel;
    QPushButton *m_closeBtn;
    QString      m_url;
};

#endif // QRCODEDIALOG_H
