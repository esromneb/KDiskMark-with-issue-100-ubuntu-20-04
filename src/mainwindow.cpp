#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDebug>
#include <QProcess>
#include <QToolTip>
#include <QMessageBox>
#include <QThread>
#include <QStorageInfo>
#include <QStandardItemModel>

#include "math.h"
#include "about.h"

MainWindow::MainWindow(AppSettings *settings, Benchmark *benchmark, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setWindowIcon(QIcon("icons/kdiskmark.svg"));

    statusBar()->setSizeGripEnabled(false);

    m_settings = settings;

    // Default values
    ui->loopsCount->setValue(m_settings->getLoopsCount());

    QActionGroup *timeIntervalGroup = new QActionGroup(this);

    ui->action0_sec->setProperty("interval", 0);
    ui->action1_sec->setProperty("interval", 1);
    ui->action3_sec->setProperty("interval", 3);
    ui->action5_sec->setProperty("interval", 5);
    ui->action10_sec->setProperty("interval", 10);
    ui->action30_sec->setProperty("interval", 30);
    ui->action1_min->setProperty("interval", 60);
    ui->action3_min->setProperty("interval", 180);
    ui->action5_min->setProperty("interval", 300);
    ui->action10_min->setProperty("interval", 600);

    ui->action0_sec->setActionGroup(timeIntervalGroup);
    ui->action1_sec->setActionGroup(timeIntervalGroup);
    ui->action3_sec->setActionGroup(timeIntervalGroup);
    ui->action5_sec->setActionGroup(timeIntervalGroup);
    ui->action10_sec->setActionGroup(timeIntervalGroup);
    ui->action30_sec->setActionGroup(timeIntervalGroup);
    ui->action1_min->setActionGroup(timeIntervalGroup);
    ui->action3_min->setActionGroup(timeIntervalGroup);
    ui->action5_min->setActionGroup(timeIntervalGroup);
    ui->action10_min->setActionGroup(timeIntervalGroup);

    connect(timeIntervalGroup, SIGNAL(triggered(QAction*)), this, SLOT(timeIntervalSelected(QAction*)));

    int size = 16;
    for (int i = 0; i < 10; i++) {
        size *= 2;
        ui->comboBox->addItem(QLatin1String("%1 %2").arg(QString::number(size), tr("MiB")));
    }

    QProgressBar* progressBars[] = {
        ui->readBar_SEQ1M_Q8T1, ui->writeBar_SEQ1M_Q8T1,
        ui->readBar_SEQ1M_Q1T1, ui->writeBar_SEQ1M_Q1T1,
        ui->readBar_RND4K_Q32T16, ui->writeBar_RND4K_Q32T16,
        ui->readBar_RND4K_Q1T1, ui->writeBar_RND4K_Q1T1
    };

    for (auto const& progressBar: progressBars) {
        progressBar->setFormat("0.00");
        progressBar->setToolTip(toolTipRaw.arg("0.000", "0.000", "0.000", "0.000"));
    }

    ui->pushButton_SEQ1M_Q8T1->setToolTip(tr("<h2>Sequential 1 MiB<br/>Queues=8<br/>Threads=1</h2>"));
    ui->pushButton_SEQ1M_Q1T1->setToolTip(tr("<h2>Sequential 1 MiB<br/>Queues=1<br/>Threads=1</h2>"));
    ui->pushButton_RND4K_Q32T16->setToolTip(tr("<h2>Random 4 KiB<br/>Queues=32<br/>Threads=16</h2>"));
    ui->pushButton_RND4K_Q1T1->setToolTip(tr("<h2>Random 4 KiB<br/>Queues=1<br/>Threads=1</h2>"));

    bool isSomeDeviceMountAsHome = false;
    bool isThereWritableDir = false;

    // Add each device and its mount point if is writable
    foreach (const QStorageInfo &storage, QStorageInfo::mountedVolumes()) {
        if (storage.isValid() && storage.isReady() && !storage.isReadOnly()) {
            if (!(storage.device().indexOf("/dev/sd") == -1 && storage.device().indexOf("/dev/nvme") == -1)) {

                if (storage.rootPath() == QDir::homePath())
                    isSomeDeviceMountAsHome = true;

                quint64 total = storage.bytesTotal();
                quint64 available = storage.bytesAvailable();

                QString path = storage.rootPath();

                ui->comboBox_Dirs->addItem(
                            tr("%1 %2% (%3)").arg(path)
                            .arg(available * 100 / total)
                            .arg(formatSize(available, total)),
                            QVariant(path)
                            );

                if (!disableDirItemIfIsNotWritable(ui->comboBox_Dirs->count() - 1)
                        && isThereWritableDir == false) {
                    isThereWritableDir = true;
                }
            }
        }
    }

    // Add home dir
    if (!isSomeDeviceMountAsHome) {
        QStorageInfo storage = QStorageInfo::root();

        quint64 total = storage.bytesTotal();
        quint64 available = storage.bytesAvailable();

        QString path = QDir::homePath();

        ui->comboBox_Dirs->insertItem(0,
                    tr("%1 %2% (%3)").arg(path)
                    .arg(storage.bytesAvailable() * 100 / total)
                    .arg(formatSize(available, total)),
                    QVariant(path)
                    );

        if (!disableDirItemIfIsNotWritable(0)
                && isThereWritableDir == false) {
            isThereWritableDir = true;
        }
    }

    if (isThereWritableDir) {
        ui->comboBox_Dirs->setCurrentIndex(0);
    }
    else {
        ui->comboBox_Dirs->setCurrentIndex(-1);
        ui->pushButton_All->setEnabled(false);
        ui->pushButton_SEQ1M_Q8T1->setEnabled(false);
        ui->pushButton_SEQ1M_Q1T1->setEnabled(false);
        ui->pushButton_RND4K_Q32T16->setEnabled(false);
        ui->pushButton_RND4K_Q1T1->setEnabled(false);
    }

    // Move Benchmark to another thread and set callbacks
    m_benchmark = benchmark;
    benchmark->moveToThread(&m_benchmarkThread);
    connect(this, &MainWindow::runBenchmark, m_benchmark, &Benchmark::runBenchmark);
    connect(m_benchmark, &Benchmark::runningStateChanged, this, &MainWindow::benchmarkStateChanged);
    connect(m_benchmark, &Benchmark::benchmarkStatusUpdated, this, &MainWindow::benchmarkStatusUpdated);
    connect(m_benchmark, &Benchmark::resultReady, this, &MainWindow::handleResults);
    connect(m_benchmark, &Benchmark::failed, this, &MainWindow::benchmarkFailed);
    connect(m_benchmark, &Benchmark::finished, &m_benchmarkThread, &QThread::terminate);

    // About button
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::showAbout);
}

MainWindow::~MainWindow()
{
    m_benchmarkThread.quit();
    m_benchmarkThread.wait();
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *)
{
    m_benchmark->setRunning(false);
}

bool MainWindow::disableDirItemIfIsNotWritable(int index)
{
    if (!QFileInfo(ui->comboBox_Dirs->itemData(index).toString()).isWritable()) {
        const QStandardItemModel* model =
                dynamic_cast<QStandardItemModel*>(ui->comboBox_Dirs->model());
        QStandardItem* item = model->item(index);
        item->setEnabled(false);

        return true;
    }
    else return false;
}

QString MainWindow::formatSize(quint64 available, quint64 total)
{
    QStringList units = { "Bytes", "KiB", "MiB", "GiB", "TiB", "PiB" };
    int i;
    double outputAvailable = available;
    double outputTotal = total;
    for(i = 0; i < units.size() - 1; i++) {
        if (outputTotal < 1024) break;
        outputAvailable = outputAvailable / 1024;
        outputTotal = outputTotal / 1024;
    }
    return QString("%1/%2 %3").arg(outputAvailable, 0, 'f', 2)
            .arg(outputTotal, 0, 'f', 2).arg(units[i]);
}

void MainWindow::timeIntervalSelected(QAction* act)
{
    m_settings->setIntervalTime(act->property("interval").toInt());
}

void MainWindow::on_loopsCount_valueChanged(int arg1)
{
    m_settings->setLoopsCount(arg1);
}

void MainWindow::on_comboBox_Dirs_currentIndexChanged(int index)
{
    m_settings->setDir(ui->comboBox_Dirs->itemData(index).toString());
}

void MainWindow::benchmarkStateChanged(bool state)
{
    if (state) {
        ui->pushButton_All->setText(tr("Stop"));
        ui->pushButton_SEQ1M_Q8T1->setText(tr("Stop"));
        ui->pushButton_SEQ1M_Q1T1->setText(tr("Stop"));
        ui->pushButton_RND4K_Q32T16->setText(tr("Stop"));
        ui->pushButton_RND4K_Q1T1->setText(tr("Stop"));
    }
    else {
        setWindowTitle(qAppName());
        ui->pushButton_All->setText(tr("All"));
        ui->pushButton_SEQ1M_Q8T1->setText("SEQ1M\nQ8T1");
        ui->pushButton_SEQ1M_Q1T1->setText("SEQ1M\nQ1T1");
        ui->pushButton_RND4K_Q32T16->setText("RND4K\nQ32T16");
        ui->pushButton_RND4K_Q1T1->setText("RND4K\nQ1T1");
        ui->pushButton_All->setEnabled(true);
        ui->pushButton_SEQ1M_Q8T1->setEnabled(true);
        ui->pushButton_SEQ1M_Q1T1->setEnabled(true);
        ui->pushButton_RND4K_Q32T16->setEnabled(true);
        ui->pushButton_RND4K_Q1T1->setEnabled(true);
    }

    m_isBenchmarkThreadRunning = state;
}

void MainWindow::showAbout()
{
    About about(m_benchmark->FIOVersion());
    about.setFixedSize(about.size());
    about.exec();
}

void MainWindow::runOrStopBenchmarkThread()
{
    if (m_isBenchmarkThreadRunning) {
        m_benchmark->setRunning(false);
        benchmarkStatusUpdated(tr("Stopping..."));
    }
    else {
        if (m_settings->getBenchmarkFile().isNull()) {
            QMessageBox::critical(this, tr("Not available"), "Directory is not specified.");
        }
        else if (QMessageBox::Yes ==
                QMessageBox::warning(this, tr("Confirmation"),
                                     tr("This action destroys the data in %1\nDo you want to continue?")
                                     .arg(m_settings->getBenchmarkFile()),
                                     QMessageBox::Yes | QMessageBox::No)) {
            m_benchmark->setRunning(true);
            m_benchmarkThread.start();
        }
    }
}

void MainWindow::benchmarkFailed(const QString &error)
{
    QMessageBox::critical(this, tr("Benchmark Failed"), error);
}

void MainWindow::benchmarkStatusUpdated(const QString &name)
{
    if (m_isBenchmarkThreadRunning)
        setWindowTitle(QLatin1String("%1 - %2").arg(qAppName(), name));
}

void MainWindow::handleResults(QProgressBar *progressBar, const Benchmark::PerformanceResult &result)
{
    progressBar->setFormat(QString::number(result.Bandwidth, 'f', 2));
    progressBar->setValue(result.Bandwidth == 0 ? 0 : 16.666666666666 * log10(result.Bandwidth * 10));
    progressBar->setToolTip(
                toolTipRaw.arg(
                    QString::number(result.Bandwidth, 'f', 3),
                    QString::number(result.Bandwidth / 1000, 'f', 3),
                    QString::number(result.IOPS, 'f', 3),
                    QString::number(result.Latency, 'f', 3)
                    )
                );
}

void MainWindow::on_pushButton_SEQ1M_Q8T1_clicked()
{
    runOrStopBenchmarkThread();

    if (m_isBenchmarkThreadRunning) {
        runBenchmark(QMap<Benchmark::Type, QProgressBar*> {
                         { Benchmark::SEQ1M_Q8T1_Read,  ui->readBar_SEQ1M_Q8T1 },
                         { Benchmark::SEQ1M_Q8T1_Write, ui->writeBar_SEQ1M_Q8T1 }
                     });
    }
}

void MainWindow::on_pushButton_SEQ1M_Q1T1_clicked()
{
    runOrStopBenchmarkThread();

    if (m_isBenchmarkThreadRunning) {
        runBenchmark(QMap<Benchmark::Type, QProgressBar*> {
                         { Benchmark::SEQ1M_Q1T1_Read,  ui->readBar_SEQ1M_Q1T1 },
                         { Benchmark::SEQ1M_Q1T1_Write, ui->writeBar_SEQ1M_Q1T1 }
                     });
    }
}

void MainWindow::on_pushButton_RND4K_Q32T16_clicked()
{
    runOrStopBenchmarkThread();

    if (m_isBenchmarkThreadRunning) {
        runBenchmark(QMap<Benchmark::Type, QProgressBar*> {
                         { Benchmark::RND4K_Q32T16_Read,  ui->readBar_RND4K_Q32T16 },
                         { Benchmark::RND4K_Q32T16_Write, ui->writeBar_RND4K_Q32T16 }
                     });
    }
}

void MainWindow::on_pushButton_RND4K_Q1T1_clicked()
{
    runOrStopBenchmarkThread();

    if (m_isBenchmarkThreadRunning) {
        runBenchmark(QMap<Benchmark::Type, QProgressBar*> {
                         { Benchmark::RND4K_Q1T1_Read,  ui->readBar_RND4K_Q1T1 },
                         { Benchmark::RND4K_Q1T1_Write, ui->writeBar_RND4K_Q1T1 }
                     });
    }
}

void MainWindow::on_pushButton_All_clicked()
{
    runOrStopBenchmarkThread();

    if (m_isBenchmarkThreadRunning) {
        runBenchmark(QMap<Benchmark::Type, QProgressBar*> {
                         { Benchmark::SEQ1M_Q8T1_Read,    ui->readBar_SEQ1M_Q8T1 },
                         { Benchmark::SEQ1M_Q8T1_Write,   ui->writeBar_SEQ1M_Q8T1 },
                         { Benchmark::SEQ1M_Q1T1_Read,    ui->readBar_SEQ1M_Q1T1 },
                         { Benchmark::SEQ1M_Q1T1_Write,   ui->writeBar_SEQ1M_Q1T1 },
                         { Benchmark::RND4K_Q32T16_Read,  ui->readBar_RND4K_Q32T16 },
                         { Benchmark::RND4K_Q32T16_Write, ui->writeBar_RND4K_Q32T16 },
                         { Benchmark::RND4K_Q1T1_Read,    ui->readBar_RND4K_Q1T1 },
                         { Benchmark::RND4K_Q1T1_Write,   ui->writeBar_RND4K_Q1T1 }
                     });
    }
}
