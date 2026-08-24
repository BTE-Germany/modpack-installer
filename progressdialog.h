#ifndef PROGRESSDIALOG_H
#define PROGRESSDIALOG_H

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;

/**
 * Modal dialog shown while the modpack is being installed. Cancelling only
 * requests the abort - the dialog stays open until the worker confirms.
 */
class ProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProgressDialog(QWidget *parent = nullptr);

public slots:
    void setStatus(const QString &status, const QString &detail);
    /// Progress from 0 to 1000; a value below zero switches to a busy indicator.
    void setProgress(int permille);
    void markCancelling();

signals:
    void cancelRequested();

protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;

private:
    void requestCancel();

    QLabel *animationLabel;
    QLabel *statusLabel;
    QLabel *detailLabel;
    QProgressBar *progressBar;
    QPushButton *cancelButton;
    QString detailText;
    bool cancelling = false;
};

#endif // PROGRESSDIALOG_H
