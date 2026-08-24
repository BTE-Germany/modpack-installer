#include "progressdialog.h"

#include <QCloseEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMovie>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

ProgressDialog::ProgressDialog(QWidget *parent)
    : QDialog(parent)
    , animationLabel(new QLabel(this))
    , statusLabel(new QLabel(this))
    , detailLabel(new QLabel(this))
    , progressBar(new QProgressBar(this))
    , cancelButton(new QPushButton(tr("Abbrechen"), this))
{
    setWindowTitle(tr("Modpack wird installiert"));
    setModal(true);
    setMinimumWidth(520);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    auto *movie = new QMovie(QStringLiteral(":/bte_images/assets/logo_animated.gif"), QByteArray(), this);
    if (movie->isValid()) {
        movie->setScaledSize(QSize(56, 56));
        animationLabel->setMovie(movie);
        movie->start();
    } else {
        animationLabel->setPixmap(QPixmap(QStringLiteral(":/bte_images/assets/logo.png"))
                                      .scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    animationLabel->setFixedSize(56, 56);

    QFont statusFont = statusLabel->font();
    statusFont.setBold(true);
    statusLabel->setFont(statusFont);
    statusLabel->setText(tr("Installation wird vorbereitet..."));

    detailLabel->setStyleSheet(QStringLiteral("color: rgb(149, 149, 149)"));
    detailLabel->setMinimumWidth(360);

    progressBar->setRange(0, 1000);
    progressBar->setValue(0);
    progressBar->setTextVisible(false);

    auto *textLayout = new QVBoxLayout;
    textLayout->setSpacing(2);
    textLayout->addWidget(statusLabel);
    textLayout->addWidget(detailLabel);

    auto *headerLayout = new QHBoxLayout;
    headerLayout->setSpacing(16);
    headerLayout->addWidget(animationLabel);
    headerLayout->addLayout(textLayout, 1);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(cancelButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(16);
    layout->addLayout(headerLayout);
    layout->addWidget(progressBar);
    layout->addLayout(buttonLayout);

    connect(cancelButton, &QPushButton::clicked, this, &ProgressDialog::requestCancel);
}

void ProgressDialog::setStatus(const QString &status, const QString &detail)
{
    if (!cancelling)
        statusLabel->setText(status);
    detailText = detail;
    detailLabel->setText(detailLabel->fontMetrics().elidedText(detail, Qt::ElideMiddle,
                                                               detailLabel->width()));
}

void ProgressDialog::setProgress(int permille)
{
    if (permille < 0) {
        progressBar->setRange(0, 0);
        return;
    }
    if (progressBar->maximum() == 0)
        progressBar->setRange(0, 1000);
    progressBar->setValue(permille);
}

void ProgressDialog::markCancelling()
{
    cancelling = true;
    cancelButton->setEnabled(false);
    statusLabel->setText(tr("Installation wird abgebrochen..."));
    detailLabel->clear();
    progressBar->setRange(0, 0);
}

void ProgressDialog::requestCancel()
{
    if (cancelling)
        return;
    markCancelling();
    emit cancelRequested();
}

void ProgressDialog::closeEvent(QCloseEvent *event)
{
    // Closing the window must not leave a half installed modpack behind.
    event->ignore();
    requestCancel();
}

void ProgressDialog::reject()
{
    requestCancel();
}
