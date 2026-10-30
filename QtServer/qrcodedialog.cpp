
#include "qrcodedialog.h"

QRCodeDialog::QRCodeDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("手机扫码访问");
    setFixedSize(420, 540);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);

    // ① 提示文字
    m_hintLabel = new QLabel("手机扫码访问", this);
    m_hintLabel->setAlignment(Qt::AlignCenter);
    m_hintLabel->setStyleSheet("font-size: 22px; font-weight: bold; color: #333;");

    layout->addWidget(m_hintLabel);

    // ② 二维码
    m_qrLabel = new QLabel(this);
    m_qrLabel->setFixedSize(360, 360);
    m_qrLabel->setAlignment(Qt::AlignCenter);
    m_qrLabel->setStyleSheet("background: white; border: 2px solid #e0e0e0; border-radius: 8px;");

    layout->addWidget(m_qrLabel, 0, Qt::AlignCenter);

    // ③ URL 文字
    QLabel *urlLabel = new QLabel(this);
    urlLabel->setObjectName("urlLabel");
    urlLabel->setAlignment(Qt::AlignCenter);
    urlLabel->setStyleSheet("font-size: 12px; color: #666;");
    urlLabel->setWordWrap(true);

    layout->addWidget(urlLabel);

    // ④ 关闭按钮
    m_closeBtn = new QPushButton("关闭", this);
    m_closeBtn->setFixedHeight(40);
    m_closeBtn->setStyleSheet( "QPushButton { background: #0078d4; color: white; "
                                "border: none; border-radius: 6px; font-size: 15px; }"
                                "QPushButton:hover { background: #106ebe; }");

    connect(m_closeBtn, &QPushButton::clicked,
            this, &QRCodeDialog::onCloseBtnClicked);

    layout->addWidget(m_closeBtn);

    setLayout(layout);
}

void QRCodeDialog::setQRPixmap(const QPixmap &pix)
{
    m_qrLabel->setPixmap(pix);
    m_qrLabel->setScaledContents(true);
}

void QRCodeDialog::setHintText(const QString &text)
{
    m_hintLabel->setText(text);
}

void QRCodeDialog::onCloseBtnClicked()
{
    this->accept();   // 关闭对话框
}
