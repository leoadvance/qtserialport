#include "maindialog.h"
#include "ui_maindialog.h"

#include <QtGui/QMessageBox>
#include <QtCore/QSettings>
#include <QtCore/QTimer>
#include <QtCore/QFile>

#include "unittests.h"
#include "serialportinfo.h"


// Logger

/* Public methods */

Logger::Logger(QObject *parent)
    : QObject(parent), m_file(new QFile(this))
{
}

void Logger::setFileName(const QString &name)
{
    m_file->setFileName(name);
}

void Logger::addContent(const QString &content, bool clearAll)
{
    QIODevice::OpenMode mode =
            QIODevice::WriteOnly | QIODevice::Text;
    mode |= (clearAll) ?
                QIODevice::Truncate : QIODevice::Append;

    if (m_file->open(mode)) {
        m_file->write(content.toLocal8Bit());
        m_file->close();
    }
}


// UnitTestBase

/* Public methods */

UnitTestBase::UnitTestBase(UnitID id, Logger *logger, QObject *parent)
    : QObject(parent), m_id(id), m_logger(logger)
    , m_srcPort(0), m_dstPort(0)
{
    Q_ASSERT(logger);
    m_enableParam = "%1/enable";
}

void UnitTestBase::setPair(const QString &src, const QString &dst)
{
    m_srcPortName = src;
    m_dstPortName = dst;
}

void UnitTestBase::setEnable(bool enable)
{
    QSettings settings;
    settings.setValue(m_enableParam.arg(m_id), enable);
}

bool UnitTestBase::isEnabled() const
{
    QSettings settings;
    return settings.value(m_enableParam.arg(m_id)).toBool();
}

int UnitTestBase::id() const
{
    return m_id;
}

QString UnitTestBase::name() const
{
    return m_name;
}

QString UnitTestBase::description() const
{
    return m_description;
}


// UnitTestFactory

/* Public methods */

UnitTestBase *UnitTestFactory::create(UnitTestBase::UnitID id, Logger *logger)
{
    switch (id) {
    case UnitTestBase::InfoUnitId:
        return new UnitTestInfo(logger);
    default:;
    }

    return 0;
}


// TestsViewModel

/* Public methods */

TestsViewModel::TestsViewModel(const QList<UnitTestBase *> &list, QObject *parent)
    : QAbstractListModel(parent)
{
    m_testsList = list;
}

int TestsViewModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_testsList.count();
}

QVariant TestsViewModel::data(const QModelIndex &index, int role) const
{
    if (index.isValid()
            && (index.row() < m_testsList.count())) {
        UnitTestBase *item = m_testsList.at(index.row());
        if (role == Qt::DisplayRole)
            return item->name();
        if (role == Qt::CheckStateRole)
            return item->isEnabled() ? Qt::Checked : Qt::Unchecked;
    }
    return QVariant();
}

QVariant TestsViewModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    Q_UNUSED(section);
    Q_UNUSED(orientation);
    Q_UNUSED(role);
    return QVariant();
}

Qt::ItemFlags TestsViewModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags flag = Qt::ItemIsEnabled;
    if (index.isValid())
        flag |= Qt::ItemIsUserCheckable | Qt::ItemIsSelectable;
    return flag;
}

bool TestsViewModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.isValid()) {
        UnitTestBase *item = m_testsList.at(index.row());
        if (role == Qt::CheckStateRole) {
            bool enable = value.toBool();
            if (item->isEnabled() != enable) {
                item->setEnable(enable);
                emit dataChanged(index, index);
                return true;
            }
        }
    }
    return false;
}


// MainDialog

/* Public methods */

MainDialog::MainDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::MainDialog)
    , m_enabledTestsCount(0), m_it(0)
{
    ui->setupUi(this);

    m_logger = new Logger(this);

    fillPairs();
    showSettings();
    createAvailableTests();

    m_model = new TestsViewModel(m_testsList, this);
    ui->listView->setModel(m_model);

    connect(ui->logLineEdit, SIGNAL(textChanged(QString)),
            this, SLOT(procLogChanged(QString)));
    connect(ui->clearLogCheckBox, SIGNAL(clicked(bool)),
            this, SLOT(procClearLogOnStartChanged(bool)));
    connect(ui->breakAllCheckBox, SIGNAL(clicked(bool)),
            this, SLOT(procBreakAllOnErrorChanged(bool)));

    connect(ui->startButton, SIGNAL(clicked()),
            this, SLOT(procStartButtonClick()));
}

MainDialog::~MainDialog()
{
    delete ui;
}

/* Private slots */

void MainDialog::procLogChanged(const QString &log)
{
    QSettings settings;
    settings.setValue(logFileSettingsKey, log);
}

void MainDialog::procClearLogOnStartChanged(bool enable)
{
    QSettings settings;
    settings.setValue(clearLogOnStartSettingsKey, enable);
}

void MainDialog::procBreakAllOnErrorChanged(bool enable)
{
    QSettings settings;
    settings.setValue(breakOnErrorSettingsKey, enable);
}

void MainDialog::procStartButtonClick()
{
    // Check pair
    if (ui->srcComboBox->currentText() == ui->dstComboBox->currentText())
        return;

    // Get enabled tests num
    m_enabledTestsCount = 0;
    foreach (UnitTestBase *test, m_testsList) {
        if (test->isEnabled())
            ++m_enabledTestsCount;
    }
    if (!m_enabledTestsCount)
        return;

    ui->progressBar->setValue(0);
    ui->progressBar->setMaximum(m_enabledTestsCount);

    m_logger->setFileName(qApp->applicationDirPath()
                          + "/"
                          + ui->logLineEdit->text());

    // Start run
    QString header(tr("\n*** S T A R T E D ***\n"));
    m_logger->addContent(header, ui->clearLogCheckBox->isChecked());
    procTestStarted();
    enableUi(false);
}

void MainDialog::procTestStarted()
{
    UnitTestBase *test = m_testsList.at(m_it++);
    if (test->isEnabled()) {
        test->setPair(ui->srcComboBox->currentText(),
                      ui->dstComboBox->currentText());
        QTimer::singleShot(1000, test, SLOT(start()));
    }
    else
        procTestStarted();
}

void MainDialog::procTestFinished()
{
    ui->progressBar->setValue(ui->progressBar->maximum() - (--m_enabledTestsCount));
    if (m_enabledTestsCount == 0) {
        enableUi(true);
        m_it = 0;

        QString header(tr("\n*** S T O P P E D ***\n"));
        m_logger->addContent(header);
        return;
    }
    else
        procTestStarted();
}

/* Private methods */

const QString MainDialog::logFileSettingsKey = "MainDialog/logFileName";
const QString MainDialog::breakOnErrorSettingsKey = "MainDialog/breakOnError";
const QString MainDialog::clearLogOnStartSettingsKey = "MainDialog/clearLogOnStart";

void MainDialog::showSettings()
{
    QSettings settings;
    ui->logLineEdit->setText(settings.value(logFileSettingsKey).toString());
    ui->clearLogCheckBox->setChecked(settings.value(clearLogOnStartSettingsKey).toBool());
    ui->breakAllCheckBox->setChecked(settings.value(clearLogOnStartSettingsKey).toBool());
}

// Called only in constructor!
void MainDialog::createAvailableTests()
{
    //
    m_testsList.append(UnitTestFactory::create(UnitTestBase::InfoUnitId, m_logger));


    foreach(UnitTestBase *test, m_testsList) {
        connect(test, SIGNAL(finished()), this, SLOT(procTestFinished()));
    }
}

// Called only in constructor!
void MainDialog::fillPairs()
{
    QStringList list;
    foreach (SerialPortInfo inf, SerialPortInfo::availablePorts()) {
        if (inf.isValid() && !inf.isBusy())
            list.append(inf.portName());
    }
    ui->srcComboBox->addItems(list);
    ui->dstComboBox->addItems(list);
}

void MainDialog::enableUi(bool enable)
{
    ui->scrollArea->setEnabled(enable);
    ui->startButton->setEnabled(enable);
}
