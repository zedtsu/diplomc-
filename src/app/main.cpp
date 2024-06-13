#include "ololordwebapp.h"

#include "ololordwebappthread.h"

#include <board/abstractboard.h>
#include <cache.h>
#include <captcha/abstractcaptchaengine.h>
#include <database.h>
#include <ololordapplication.h>
#include <search.h>
#include <settingslocker.h>
#include <stored/RegisteredUser>
#include <tools.h>

#include <BApplicationServer>
#include <BCoreApplication>
#include <BDirTools>
#include <BeQt>
#include <BLocationProvider>
#include <BLogger>
#include <BSettingsNode>
#include <BTerminal>
#include <BTextTools>
#include <BTranslation>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QRegExp>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <cppcms/json.h>

B_DECLARE_TRANSLATE_FUNCTION

static const QString IpAddressRegexpPattern =
        "(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])";
static const QString LogDateTimeFormat = "yyyy.MM.dd hh:mm:ss.zzz";
static const QString LogFileDateTimeFormat = "yyyy.MM.dd-hh.mm.ss";

static bool checkParsingError(BTextTools::OptionsParsingError error, const QString &errorData);
static bool handleBanPoster(const QString &cmd, const QStringList &args);
static bool handleBanUser(const QString &cmd, const QStringList &args);
static bool handleCache(const QString &cmd, const QStringList &args);
static bool handleClearCache(const QString &cmd, const QStringList &args);
static bool handleCloseThread(const QString &cmd, const QStringList &args);
static bool handleDeletePost(const QString &cmd, const QStringList &args);
static bool handleFixThread(const QString &cmd, const QStringList &args);
static bool handleNewLog(const QString &cmd, const QStringList &args);
static bool handleOpenThread(const QString &cmd, const QStringList &args);
static bool handleRebuildPostIndex(const QString &cmd, const QStringList &args);
static bool handleRegisterUser(const QString &cmd, const QStringList &args);
static bool handleReloadBoards(const QString &cmd, const QStringList &args);
static bool handleReloadCaptchaEngines(const QString &cmd, const QStringList &args);
static bool handleRerenderPosts(const QString &cmd, const QStringList &args);
static bool handleSet(const QString &cmd, const QStringList &args);
static bool handleShowPoster(const QString &cmd, const QStringList &args);
static bool handleUnfixThread(const QString &cmd, const QStringList &args);
static bool handleUptime(const QString &cmd, const QStringList &args);
static void initCommands();
static void initSettings();
static void initTerminal();
static QString logFileName();
static QString msecsToString(qint64 msecs);
static bool setDefaultThreadPassword(const BSettingsNode *node, const QVariant &value);
static bool setLoggingMode(const BSettingsNode *, const QVariant &v);
static bool setLoggingSkipIp(const BSettingsNode *node, const QVariant &value);
static bool setMaxCacheSize(const BSettingsNode *node, const QVariant &value);
static bool showDefaultThreadPassword(const BSettingsNode *node, const QVariant &value);
static void updateLoggingMode();

int main(int argc, char **argv)
{
    static const QString AppName = "ololord";
    QString home = QDir::home().dirName();
    BApplicationServer s(9710 + qHash(home) % 10, AppName + "0" + home);
    int ret = 0;
    bool force = (argc > 1) && (QString::fromLocal8Bit(argv[1]) == "--force-launch");
    if (!s.testServer() || force) {
        OlolordApplication app(argc, argv, AppName, "Andrey Bogdanov");
        if (!force)
            s.listen();
        app.setApplicationVersion("0.1.0-rc11");
        BLocationProvider *prov = new BLocationProvider;
        prov->addLocation("storage");
        prov->addLocation("storage/img");
        prov->createLocationPath("storage", BCoreApplication::UserResource);
        prov->createLocationPath("storage/img", BCoreApplication::UserResource);
        app.installLocationProvider(prov);
        BCoreApplication::installBeqtTranslator("qt");
        BCoreApplication::installBeqtTranslator("beqt");
        BCoreApplication::installBeqtTranslator("ololord");
        initTerminal();
        AbstractCaptchaEngine::reloadEngines();
        AbstractBoard::reloadBoards();
        AbstractBoard::restoreCaptchaQuota(BDirTools::readFile(Tools::captchaQuotaFile()));
        Search::restoreIndex(BDirTools::readFile(Tools::searchIndexFile()));
        bLogger->setDateTimeFormat(LogDateTimeFormat);
        bLogger->setFileName(logFileName());
        updateLoggingMode();
        bWriteLine(translate("main", "This is") + " " + BCoreApplication::applicationName()
                   + " v" + BCoreApplication::applicationVersion());
        bWriteLine(translate("main", "Enter \"help --commands\" to see the list of available commands"));
        BCoreApplication::loadPlugins(QStringList() << "route-factory" << "ajax-handler-factory"
                                      << "captcha-engine-factory");
        QString confFileName = BDirTools::findResource("res/config.js", BDirTools::AllResources);
        bool ok = false;
        cppcms::json::value conf = Tools::readJsonValue(confFileName, &ok);
        if (!ok) {
            bWriteLine(translate("main", "Failed to read configuration file"));
            return 0;
        }
        Database::createSchema();
        Database::checkOutdatedEntries();
        Database::generateRss();
        OlolordWebAppThread owt(conf);
        owt.start();
        ret = app.exec();
        owt.shutdown();
        owt.wait(10 * BeQt::Second);
        BDirTools::writeFile(Tools::captchaQuotaFile(), AbstractBoard::saveCaptchaQuota());
        BDirTools::writeFile(Tools::searchIndexFile(), Search::saveIndex());
        foreach (const QString &name, Cache::availableCacheNames())
            Cache::clearCache(name);
    } else {
        bWriteLine(translate("main", "Another instance of") + " "  + AppName + " "
                   + translate("main", "is already running. Quitting..."));
    }
    return ret;
}

bool checkParsingError(BTextTools::OptionsParsingError error, const QString &errorData)
{
    switch (error) {
    case BTextTools::InvalidParametersError:
        bWriteLine(translate("checkParsingError", "Internal parsing error", "error"));
        return false;
    case BTextTools::MalformedOptionError:
        bWriteLine(translate("checkParsingError", "Malformed option:", "error") + " " + errorData);
        return false;
    case BTextTools::MissingOptionError:
        bWriteLine(translate("checkParsingError", "Missing option:", "error") + " " + errorData);
        return false;
    case BTextTools::RepeatingOptionError:
        bWriteLine(translate("checkParsingError", "Repeating option:", "error") + " " + errorData);
        return false;
    case BTextTools::UnknownOptionError:
        bWriteLine(translate("checkParsingError", "Unknown option:", "error") + " " + errorData);
        return false;
    case BTextTools::UnknownOptionValueError:
        bWriteLine(translate("checkParsingError", "Unknown option value:", "error") + " " + errorData);
        return false;
    case BTextTools::NoError:
    default:
        return true;
    }
}

bool handleBanPoster(const QString &, const QStringList &args)
{
    QMap<QString, QString> result;
    QString errorData;
    QString boards = AbstractBoard::boardNames().join("|");
    QString options = "sourceBoard:--source-board|-s=" + boards + ",postNumber:--post-number|-p=,"
            "[level:--level|-l=0|1|10|100],[board:--board|-b=" + boards + "],[reason:--reason|-r=],"
            "[expires:--expires|-e=]";
    BTextTools::OptionsParsingError error = BTextTools::parseOptions(args, options, result, errorData);
    if (!checkParsingError(error, errorData))
        return false;
    QString sourceBoard = result.value("sourceBoard");
    bool ok = false;
    quint64 postNumber = result.value("postNumber").toULongLong(&ok);
    if (!ok || !postNumber)
        return false;
    QString board = result.value("board");
    int level = result.contains("level") ? result.value("level").toInt() : 1;
    QString reason = result.value("reason");
    QDateTime expires = result.contains("expires") ? QDateTime::fromString(result.value("expires"),
                                                                           Tools::InputDateTimeFormat) : QDateTime();
    if (!expires.isValid() && level > 0) {
        if (result.contains("expires")) {
            bReadLine(translate("handleBanPoster", "Invalid date"));
            return false;
        }
        QString s = bReadLine(translate("handleBanPoster", "No date specified. User will be banned forever. Continue?")
                              + " [Yn] ");
        if (!s.isEmpty() && s.compare("y", Qt::CaseInsensitive)) {
            bWriteLine(translate("handleBanPoster", "Canceled"));
            return true;
        }
    }
    QString err;
    QMap<QString, Database::BanInfo> map = Database::userBanInfo(sourceBoard, postNumber, &ok, &err);
    if (!ok) {
        bWriteLine(err);
        return true;
    }
    Database::BanInfo inf;
    inf.expires = expires;
    inf.level = level;
    inf.reason = reason;
    if (board.isEmpty()) {
        QString s = bReadLine(translate("handleBanPoster",
                                        "No board specified. User will be banned/unbanned on all boards. Continue?")
                              + " [Yn] ");
        if (!s.isEmpty() && s.compare("y", Qt::CaseInsensitive)) {
            bWriteLine(translate("handleBanPoster", "Canceled"));
            return true;
        }
        foreach (const QString &bn, AbstractBoard::boardNames()) {
            inf.boardName = bn;
            map.insert(bn, inf);
        }
    } else {
        inf.boardName = board;
        map.insert(board, inf);
    }
    if (!Database::banUser(sourceBoard, postNumber, map.values(), &err))
        bWriteLine(err);
    else
        bWriteLine(translate("handleBanPoster", "OK"));
    return true;
}

bool handleBanUser(const QString &, const QStringList &args)
{
    QMap<QString, QString> result;
    QString errorData;
    QString boards = AbstractBoard::boardNames().join("|");
    QString options = "ip:--ip-address|-i=,[board:--board|-b=" + boards + "],[level:--level|-l=0|1|10|100],"
            "[reason:--reason|-r=],[expires:--expires|-e=]";
    BTextTools::OptionsParsingError error = BTextTools::parseOptions(args, options, result, errorData);
    if (!checkParsingError(error, errorData))
        return false;
    QString ip = result.value("ip");
    if (!QRegExp(IpAddressRegexpPattern).exactMatch(ip)) {
        bWriteLine(translate("handleBanUser", "Invalid IP address"));
        return false;
    }
    QString board = result.value("board");
    int level = result.contains("level") ? result.value("level").toInt() : 1;
    QString reason = result.value("reason");
    QDateTime expires = result.contains("expires") ? QDateTime::fromString(result.value("expires"),
                                                                           Tools::InputDateTimeFormat) : QDateTime();
    if (!expires.isValid() && level > 0) {
        if (result.contains("expires")) {
            bReadLine(translate("handleBanUser", "Invalid date"));
            return false;
        }
        QString s = bReadLine(translate("handleBanUser", "No date specified. User will be banned forever. Continue?")
                              + " [Yn] ");
        if (!s.isEmpty() && s.compare("y", Qt::CaseInsensitive)) {
            bWriteLine(translate("handleBanUser", "Canceled"));
            return true;
        }
    }
    QString err;
    bool ok = false;
    QMap<QString, Database::BanInfo> map = Database::userBanInfo(ip, &ok, &err);
    if (!ok) {
        bWriteLine(err);
        return true;
    }
    Database::BanInfo inf;
    inf.expires = expires;
    inf.level = level;
    inf.reason = reason;
    if (board.isEmpty()) {
        QString s = bReadLine(translate("handleBanUser",
                                        "No board specified. User will be banned/unbanned on all boards. Continue?")
                              + " [Yn] ");
        if (!s.isEmpty() && s.compare("y", Qt::CaseInsensitive)) {
            bWriteLine(translate("handleBanUser", "Canceled"));
            return true;
        }
        foreach (const QString &bn, AbstractBoard::boardNames()) {
            inf.boardName = bn;
            map.insert(bn, inf);
        }
    } else {
        inf.boardName = board;
        map.insert(board, inf);
    }
    if (!Database::banUser(ip, map.values(), &err))
        bWriteLine(err);
    else
        bWriteLine(translate("handleBanUser", "OK"));
    return true;
}

static bool handleCache(const QString &, const QStringList &args)
{
    if (args.size() > 1) {
        bWriteLine(translate("handleCache", "Invalid argument count"));
        return false;
    }
    QString s = bReadLine(translate("handleCache", "This operation is REALLY heavy and may consume A LOT OF MEMORY. "
                                    "Are you sure?") + " [yN] ");
    if (s.compare("y", Qt::CaseInsensitive))
        return true;
    static const QStringList List = QStringList() << "dynamic_files" << "static_files";
    if (args.size() && !List.contains(args.first())) {
        bWriteLine(translate("handleCache", "No such cache"));
        return false;
    }
    bool bdynamicFiles = args.isEmpty() || !args.first().compare("dynamic_files");
    bool bstaticFiles = args.isEmpty() || !args.first().compare("static_files");
    if (bdynamicFiles) {
        QString path = BCoreApplication::location("storage/img", BCoreApplication::UserResource);
        QStringList files = !path.isEmpty() ? BDirTools::entryListRecursive(path, QDir::Files) : QStringList();
        int curr = 1;
        if (files.size())
            bWriteLine(translate("handleCache", "Caching dynamic files"));
        foreach (QString fn, files) {
            QString p = fn;
            p.remove(path + "/");
            bWriteLine(QString::number(curr) + "/" + QString::number(files.size()) + ": " + p);
            ++curr;
            bool ok = false;
            QByteArray file = BDirTools::readFile(fn, -1, &ok);
            if (!ok)
                continue;
            Cache::cacheDynamicFile(p, file);
        }
    }
    if (bstaticFiles) {
        QString path1 = BCoreApplication::location(BCoreApplication::DataPath, BCoreApplication::SharedResource);
        QString path2 = BCoreApplication::location(BCoreApplication::DataPath, BCoreApplication::UserResource);
        //No need to cache builtin resources
        QStringList files;
        if (!path1.isEmpty())
            files << BDirTools::entryListRecursive(path1 + "/static", QDir::Files);
        if (!path2.isEmpty())
            files << BDirTools::entryListRecursive(path2 + "/static", QDir::Files);
        int curr = 1;
        if (files.size())
            bWriteLine(translate("handleCache", "Caching static files"));
        foreach (QString fn, files) {
            QString p = fn;
            if (!path1.isEmpty())
                p.remove(path1 + "/static/");
            if (!path2.isEmpty())
                p.remove(path2 + "/static/");
            bWriteLine(QString::number(curr) + "/" + QString::number(files.size()) + ": " + p);
            ++curr;
            bool ok = false;
            QByteArray file = BDirTools::readFile(fn, -1, &ok);
            if (!ok)
                continue;
            Cache::cacheStaticFile(p, file);
        }
    }
    bWriteLine(translate("handleClearCache", "OK"));
    return true;
}

static bool handleClearCache(const QString &, const QStringList &args)
{
    if (args.size() > 1) {
        bWriteLine(translate("handleClearCache", "Invalid argument count"));
        return false;
    }
    QString s = bReadLine(translate("handleClearCache", "Are you sure?") + " [Yn] ");
    if (!s.isEmpty() && s.compare("y", Qt::CaseInsensitive))
        return true;
    QString err;
    bool ok = false;
    if (args.size()) {
        ok = Cache::clearCache(args.first(), &err);
    } else {
        foreach (const QString &nm, Cache::availableCacheNames()) {
            ok = Cache::clearCache(nm, &err);
            if (!ok)
                break;
        }
    }
    if (!ok)
        bWriteLine(err);
    else
        bWriteLine(translate("handleClearCache", "OK"));
    return true;
}

bool handleCloseThread(const QString &, const QStringList &args)
{
    if (args.size() != 2) {
        bWriteLine(translate("handleCloseThread", "Invalid argument count"));
        return false;
    }
    QString boardName = args.first().toLower();
    if (!AbstractBoard::boardNames().contains(boardName, Qt::CaseInsensitive)) {
        bWriteLine(translate("handleCloseThread", "Invalid board name"));
        return false;
    }
    bool ok = false;
    quint64 threadNumber = args.last().toULongLong(&ok);
    if (!ok || !threadNumber) {
        bWriteLine(translate("handleCloseThread", "Invalid thread number"));
        return false;
    }
    QString err;
    if (!Database::setThreadOpened(boardName, threadNumber, false, &err))
        bWriteLine(err);
    bWriteLine(translate("handleCloseThread", "OK"));
    return true;
}

bool handleDeletePost(const QString &, const QStringList &args)
{
    if (args.size() != 2) {
        bWriteLine(translate("handleDeletePost", "Invalid argument count"));
        return false;
    }
    QString boardName = args.first().toLower();
    if (!AbstractBoard::boardNames().contains(boardName, Qt::CaseInsensitive)) {
        bWriteLine(translate("handleDeletePost", "Invalid board name"));
        return false;
    }
    bool ok = false;
    quint64 postNumber = args.last().toULongLong(&ok);
    if (!ok || !postNumber) {
        bWriteLine(translate("handleDeletePost", "Invalid thread number"));
        return false;
    }
    QString s = bReadLine(translate("handleDeletePost", "Are you sure?") + " [Yn] ");
    if (!s.isEmpty() && s.compare("y", Qt::CaseInsensitive))
        return true;
    QString err;
    if (!Database::deletePost(boardName, postNumber, &err))
        bWriteLine(err);
    else
        bWriteLine(translate("handleDeletePost", "OK"));
    return true;
}

bool handleFixThread(const QString &, const QStringList &args)
{
    if (args.size() != 2) {
        bWriteLine(translate("handleFixThread", "Invalid argument count"));
        return false;
    }
    QString boardName = args.first().toLower();
    if (!AbstractBoard::boardNames().contains(boardName, Qt::CaseInsensitive)) {
        bWriteLine(translate("handleFixThread", "Invalid board name"));
        return false;
    }
    bool ok = false;
    quint64 threadNumber = args.last().toULongLong(&ok);
    if (!ok || !threadNumber) {
        bWriteLine(translate("handleFixThread", "Invalid thread number"));
        return false;
    }
    QString err;
    if (!Database::setThreadFixed(boardName, threadNumber, true, &err))
        bWriteLine(err);
    else
        bWriteLine(translate("handleFixThread", "OK"));
    return true;
}

bool handleNewLog(const QString &, const QStringList &)
{
    QString s = bReadLine(translate("handleNewLog", "Are you sure?") + " [Yn] ");
    if (!s.isEmpty() && s.compare("y", Qt::CaseInsensitive))
        return true;
    bLogger->setFileName(logFileName());
    bWriteLine(translate("handleNewLog", "OK"));
    return true;
}

bool handleOpenThread(const QString &, const QStringList &args)
{
    if (args.size() != 2) {
        bWriteLine(translate("handleOpenThread", "Invalid argument count"));
        return false;
    }
    QString boardName = args.first().toLower();
    if (!AbstractBoard::boardNames().contains(boardName, Qt::CaseInsensitive)) {
        bWriteLine(translate("handleOpenThread", "Invalid board name"));
        return false;
    }
    bool ok = false;
    quint64 threadNumber = args.last().toULongLong(&ok);
    if (!ok || !threadNumber) {
        bWriteLine(translate("handleOpenThread", "Invalid thread number"));
        return false;
    }
    QString err;
    if (!Database::setThreadOpened(boardName, threadNumber, true, &err))
        bWriteLine(err);
    else
        bWriteLine(translate("handleOpenThread", "OK"));
    return true;
}

bool handleRebuildPostIndex(const QString &, const QStringList &)
{
    QString s = bReadLine(translate("handleRebuildPostIndex", "Are you sure?") + " [Yn] ");
    if (!s.isEmpty() && s.compare("y", Qt::CaseInsensitive))
        return true;
    QString err;
    int count = Search::rebuildIndex(&err);
    if (count < 0)
        bWriteLine(translate("handleRebuildPostIndex", "Error:") + " " + err);
    else
        bWriteLine(translate("handleRebuildPostIndex", "Rebuilt index of posts:") +  " " + QString::number(count));
    return true;
}

bool handleRegisterUser(const QString &, const QStringList &)
{
    QString pwd = bReadLineSecure(translate("handleRegisterUser", "Enter password:") + " ");
    if (pwd.isEmpty()) {
        bWriteLine(translate("handleRegisterUser", "Invalid password"));
        return false;
    }
    QString lvl = bReadLine(translate("handleRegisterUser", "Enter level:\n"
                                      "0 - no (just logs in)\n"
                                      "1 - user level\n"
                                      "10 - moder level\n"
                                      "100 - admin level\n"
                                      "Your choice:") + " ");
    bool ok = false;
    int level = lvl.toInt(&ok);
    if (!ok || (0 != level && 1 != level && 10 != level && 100 != level)) {
        bWriteLine(translate("handleRegisterUser", "Invalid level"));
        return false;
    }
    QStringList boards = bReadLine(translate("handleRegisterUser", "Enter boards:\n"
                                             "Separate board names by spaces.\n"
                                             "* - any board\n"
                                             "Your choice:") + " ").split(QRegExp("\\s+"), QString::SkipEmptyParts);
    boards.removeDuplicates();
    QByteArray password = Tools::toHashpass(pwd);
    if (password.isEmpty())
        password = QCryptographicHash::hash(pwd.toUtf8(), QCryptographicHash::Sha1);
    QString err;
    if (!Database::registerUser(password, static_cast<RegisteredUser::Level>(level), boards, &err)) {
        bWriteLine(err);
        return true;
    }
    bWriteLine(translate("handleRegisterUsers", "OK"));
    return true;
}

bool handleReloadBoards(const QString &, const QStringList &)
{
    QString s = bReadLine(translate("handleReloadBoards", "Are you sure?") + " [Yn] ");
    if (!s.isEmpty() && s.compare("y", Qt::CaseInsensitive))
        return true;
    AbstractBoard::reloadBoards();
    bWriteLine(translate("handleReloadBoards", "OK"));
    return true;
}

bool handleReloadCaptchaEngines(const QString &, const QStringList &)
{
    QString s = bReadLine(translate("handleReloadCaptchaEngines", "Are you sure?") + " [Yn] ");
    if (!s.isEmpty() && s.compare("y", Qt::CaseInsensitive))
        return true;
    AbstractCaptchaEngine::reloadEngines();
    bWriteLine(translate("handleReloadCaptchaEngines", "OK"));
    return true;
}

bool handleRerenderPosts(const QString &, const QStringList &args)
{
    QString s = bReadLine(translate("handleRerenderPosts", "This operation is REALLY heavey and may take a long time. "
                                    "Are you sure?") + " [yN] ");
    if (s.compare("y", Qt::CaseInsensitive))
        return true;
    QStringList boardNames = args;
    boardNames.removeAll("");
    boardNames.removeDuplicates();
    QString err;
    int count = Database::rerenderPosts(boardNames, &err);
    if (count < 0)
        bWriteLine(translate("handleRerenderPosts", "Error:") + " " + err);
    else
        bWriteLine(translate("handleRerenderPosts", "Rerendered posts:") +  " " + QString::number(count));
    return true;
}

bool handleSet(const QString &cmd, const QStringList &args)
{
    SettingsLocker locker;
    Q_UNUSED(locker)
    return BTerminal::handler(BTerminal::SetCommand)(cmd, args);
}

bool handleShowPoster(const QString &, const QStringList &args)
{
    if (args.size() != 2) {
        bWriteLine(translate("showPoster", "Invalid argument count"));
        return false;
    }
    QString boardName = args.first().toLower();
    if (!AbstractBoard::boardNames().contains(boardName, Qt::CaseInsensitive)) {
        bWriteLine(translate("showPoster", "Invalid board name"));
        return false;
    }
    bool ok = false;
    quint64 postNumber = args.last().toULongLong(&ok);
    if (!ok || !postNumber) {
        bWriteLine(translate("showPoster", "Invalid post number"));
        return false;
    }
    Database::GeolocationInfo gli = Database::geolocationInfo(boardName, postNumber);
    if (gli.ip.isEmpty()) {
        bWriteLine(translate("showPoster", "No such post"));
        return false;
    }
    bWriteLine(translate("showPoster", "Poster IP:") + " " + gli.ip);
    QString cc = gli.countryCode;
    QString cn = gli.countryName;
    if (!cc.isEmpty()) {
        bWriteLine(translate("showPoster", "Poster country:") + " " + cc
                   + (!cn.isEmpty() ? ("(" + cn + ")") : QString()));
    }
    return true;
}

bool handleUnfixThread(const QString &, const QStringList &args)
{
    if (args.size() != 2) {
        bWriteLine(translate("handleUnfixThread", "Invalid argument count"));
        return false;
    }
    QString boardName = args.first().toLower();
    if (!AbstractBoard::boardNames().contains(boardName, Qt::CaseInsensitive)) {
        bWriteLine(translate("handleUnfixThread", "Invalid board name"));
        return false;
    }
    bool ok = false;
    quint64 threadNumber = args.last().toULongLong(&ok);
    if (!ok || !threadNumber) {
        bWriteLine(translate("handleUnfixThread", "Invalid thread number"));
        return false;
    }
    QString err;
    if (!Database::setThreadFixed(boardName, threadNumber, false, &err))
        bWriteLine(err);
    else
        bWriteLine(translate("handleUnfixThread", "OK"));
    return true;
}

bool handleUptime(const QString &, const QStringList &)
{
    if (!oApp) {
        bWriteLine(translate("handleUptime", "No OlolordApplication instance"));
        return false;
    }
    bWriteLine(translate("handleUptime", "Uptime:") + " " + msecsToString(oApp->uptime()));
    return true;
}

void initCommands()
{
    BTerminal::setHelpDescription(BTranslation::translate("initCommands",
                                                          "This is ololord. Enter \"help --all\" to see full Help"));
    BTerminal::installHandler(BTerminal::HelpCommand);
    BTerminal::installHandler(BTerminal::LastCommand);
    BTerminal::installHandler(BTerminal::QuitCommand);
    BTerminal::installHandler("q", BTerminal::handler(BTerminal::QuitCommand));
    BTerminal::CommandHelp ch = BTerminal::commandHelp(BTerminal::QuitCommand);
    ch.usage += "|q";
    foreach (const QString &s, BTerminal::commands(BTerminal::QuitCommand))
        BTerminal::setCommandHelp(s, ch);
    foreach (const QString &s, BTerminal::commands(BTerminal::SetCommand)) {
        BTerminal::installHandler(s, &handleSet);
        BTerminal::setCommandHelp(s, BTerminal::commandHelpList(BTerminal::SetCommand));
    }
    //
    BTerminal::installHandler("ban-user", &handleBanUser);
    ch.usage = "ban-user <parameters>";
    ch.description = BTranslation::translate("initCommands",
        "Ban a user.\n"
        "The parameters are:\n"
        "  --ip-address|-i=<ip>, where <ip> must be a valid IP address.\n"
        "  --board|-b=<board> (optional), where <board> must be one of the existing boards or *.\n"
        "    The user will be banned for the board specified or for all boards in case of *).\n"
        "    The default is *.\n"
        "  --level|-l=<level> (optional), where <level> must be one of the following:\n"
        "    0 - user is not banned,\n"
        "    1 - user is banned from posting,\n"
        "    10 - user is banned from reading.\n"
        "  --reason|-r=<reason> (optional), where <reason> is a reason for (un)banning.\n"
        "  --expires|-e=<date> (optional), where <date> must be the expiration date in the following format:\n"
        "    dd.MM.yyyy:hh - day.month.year:hour. If omitted, the ban will never expire.\n"
        "Example:\n"
        "  ban-user -i=192.168.0.2 -b=vg -l=1 \"-r=posting shit\" -e=01.04.2015:22");
    BTerminal::setCommandHelp("ban-user", ch);
    //
    BTerminal::installHandler("ban-poster", &handleBanPoster);
    ch.usage = "ban-poster <parameters>";
    ch.description = BTranslation::translate("initCommands",
        "Ban a user.\n"
        "The parameters are:\n"
        "  --source-board|-s=<board>, where <board> must be one of the existing boards.\n"
        "  --post-number|-p=<number>, where <number> must be a number of a post.\n"
        "  --board|-b=<board> (optional), where <board> must be one of the existing boards or *.\n"
        "    The user will be banned for the board specified or for all boards in case of *).\n"
        "    The default is *.\n"
        "  --level|-l=<level> (optional), where <level> must be one of the following:\n"
        "    0 - user is not banned,\n"
        "    1 - user is banned from posting,\n"
        "    10 - user is banned from reading.\n"
        "  --reason|-r=<reason> (optional), where <reason> is a reason for (un)banning.\n"
        "  --expires|-e=<date> (optional), where <date> must be the expiration date in the following format:\n"
        "    dd.MM.yyyy:hh - day.month.year:hour. If omitted, the ban will never expire.\n"
        "Example:\n"
        "  ban-poster -i=192.168.0.2 -b=vg -l=1 \"-r=posting shit\" -e=01.04.2015:22");
    BTerminal::setCommandHelp("ban-poster", ch);
    //
    BTerminal::installHandler("close-thread", &handleCloseThread);
    ch.usage = "close-thread <board> <thread-number>";
    ch.description = BTranslation::translate("initCommands",
        "Make a thread <thread-number> at <board> not available for posting (closed).");
    BTerminal::setCommandHelp("close-thread", ch);
    //
    BTerminal::installHandler("fix-thread", &handleFixThread);
    ch.usage = "fix-thread <board> <thread-number>";
    ch.description = BTranslation::translate("initCommands",
        "Make a thread <thread-number> at <board> fixed (always above regular threads).");
    BTerminal::setCommandHelp("fix-thread", ch);
    //
    BTerminal::installHandler("open-thread", &handleOpenThread);
    ch.usage = "open-thread <board> <thread-number>";
    ch.description = BTranslation::translate("initCommands",
        "Make a thread <thread-number> at <board> available for posting.");
    BTerminal::setCommandHelp("open-thread", ch);
    //
    BTerminal::installHandler("show-poster", &handleShowPoster);
    ch.usage = "show-poster <board> <post-number>";
    ch.description = BTranslation::translate("initCommands",
        "Show information about the user that made a post with <post-number> at <board>.");
    BTerminal::setCommandHelp("show-poster", ch);
    //
    BTerminal::installHandler("unfix-thread", &handleUnfixThread);
    ch.usage = "unfix-thread <board> <thread-number>";
    ch.description = BTranslation::translate("initCommands",
        "Make a thread <thread-number> at <board> not fixed (regular thread).");
    BTerminal::setCommandHelp("unfix-thread", ch);
    //
    BTerminal::installHandler("cache", &handleCache);
    ch.usage = "cache [cache-name]";
    ch.description = BTranslation::translate("initCommands", "Cache all dynamic/static files.\n"
                                             "[cache-name] may be one of the following: \n"
                                             "dynamic_files, static_files.\n"
                                             "If no argument is passed, everything is cached.");
    BTerminal::setCommandHelp("cache", ch);
    //
    BTerminal::installHandler("clear-cache", &handleClearCache);
    ch.usage = "clear-cache [cache-name]";
    BTranslation t = BTranslation::translate("initCommands", "Clear the cache specified by [cache-name].\n"
                                             "If [cache-name] is not specified, all caches are cleared.\n"
                                             "The following caches may be cleared:\n%1");
    t.setArgument("  " + Cache::availableCacheNames().join("\n  "));
    ch.description = t;
    BTerminal::setCommandHelp("clear-cache", ch);
    //
    BTerminal::installHandler("reload-boards", &handleReloadBoards);
    ch.usage = "reload-boards";
    ch.description = BTranslation::translate("initCommands", "Reload all boards: builtin and provided by plugins.");
    BTerminal::setCommandHelp("reload-boards", ch);
    //
    BTerminal::installHandler("reload-captcha-engines", &handleReloadCaptchaEngines);
    ch.usage = "reload-captcha-engines";
    ch.description = BTranslation::translate("initCommands", "Reload all captcha engines: builtin and provided by "
                                             "plugins.");
    BTerminal::setCommandHelp("reload-captcha-engines", ch);
    //
    BTerminal::installHandler("rebuild-post-index", &handleRebuildPostIndex);
    ch.usage = "rebuild-post-index";
    ch.description = BTranslation::translate("initCommands", "Clear post text index and create it from scratch.");
    BTerminal::setCommandHelp("rebuild-post-index", ch);
    //
    BTerminal::installHandler("register-user", &handleRegisterUser);
    ch.usage = "register-user";
    ch.description = BTranslation::translate("initCommands", "Registers a user.");
    BTerminal::setCommandHelp("register-user", ch);
    //
    BTerminal::installHandler("rerender-posts", &handleRerenderPosts);
    ch.usage = "rerender-posts [board]...";
    ch.description = BTranslation::translate("initCommands", "Rerenders all posts on all boards.\n"
                                             "If one or more board names are specified, rerenders only posts on those "
                                             "boards.");
    BTerminal::setCommandHelp("rerender-posts", ch);
    //
    BTerminal::installHandler("delete-post", &handleDeletePost);
    ch.usage = "delete-post <board> <post-number>";
    ch.description = BTranslation::translate("initCommands", "Delete post with <post-number> at <board>.\n"
        "If <post-number> is a thread, that thread and all posts in it are deleted.");
    BTerminal::setCommandHelp("delete-post", ch);
    //
    BTerminal::installHandler("new-log", &handleNewLog);
    ch.usage = "new-log";
    ch.description = BTranslation::translate("initCommands", "Finish writing to the current log file and start "
                                             "writing to a new one.");
    BTerminal::setCommandHelp("new-log", ch);
    //
    BTerminal::installHandler("uptime", &handleUptime);
    ch.usage = "uptime";
    ch.description = BTranslation::translate("initCommands", "Shows for how long the application has been running.");
    BTerminal::setCommandHelp("uptime", ch);
}

void initSettings()
{
    BSettingsNode *root = new BSettingsNode;
    BTerminal::createBeQtSettingsNode(root);
    /*======================================== Board ========================================*/
    BSettingsNode *n = new BSettingsNode("Board", root);
    BSettingsNode *nn = new BSettingsNode(QVariant::ByteArray, "default_post_password", n);
    nn->setUserSetFunction(&setDefaultThreadPassword);
    nn->setUserShowFunction(&showDefaultThreadPassword);
    nn->setDescription(BTranslation::translate("initSettings",
                                               "Default password for posts/threads (stored as SHA-1 hash)."));
    nn = new BSettingsNode(QVariant::Bool, "captcha_enabled", n);
    nn->setDescription(BTranslation::translate("initSettings", "Determines if captcha is enabled.\n"
                                               "If false, captcha will be disabled on all boards.\n"
                                               "The default is true."));
    nn = new BSettingsNode(QVariant::String, "supported_captcha_engines", n);
    nn->setDescription(BTranslation::translate("initSettings", "Identifiers of supported captcha engines.\n"
                                               "Identifers must be separated by commas.\n"
                                               "Example: google-recaptcha,codecha\n"
                                               "By default all captcha engines are supported."));
    nn = new BSettingsNode(QVariant::UInt, "threads_per_page", n);
    nn->setDescription(BTranslation::translate("initSettings", "Number of threads per one page.\n"
                                               "The default is 20."));
    nn = new BSettingsNode(QVariant::Bool, "posting_enabled", n);
    nn->setDescription(BTranslation::translate("initSettings", "Determines if posting is enabled.\n"
                                               "If false, posting will be disabled on all boards.\n"
                                               "The default is true."));
    nn = new BSettingsNode(QVariant::Bool, "drafts_enabled", n);
    nn->setDescription(BTranslation::translate("initSettings", "Determines if drafts are enabled.\n"
                                               "The default is true."));
    nn = new BSettingsNode(QVariant::UInt, "bump_limit", n);
    nn->setDescription(BTranslation::translate("initSettings", "Maximum bump count.\n"
                                               "When a thread has reached it's bump limit, "
                                               "it will not be raised anymore.\n"
                                               "The default is 500."));
    nn = new BSettingsNode(QVariant::UInt, "post_limit", n);
    nn->setDescription(BTranslation::translate("initSettings", "Maximum post count per thread.\n"
                                               "The default is 1000."));
    nn = new BSettingsNode(QVariant::UInt, "thread_limit", n);
    nn->setDescription(BTranslation::translate("initSettings", "Maximum thread count per board.\n"
                                               "When the limit is reached, the most old threads get deleted.\n"
                                               "The default is 200."));
    nn = new BSettingsNode(QVariant::UInt, "max_last_posts", n);
    nn->setDescription(BTranslation::translate("initSettings", "Maximum last posts displayed for each thread.\n"
                                               "The default is 3."));
    nn = new BSettingsNode(QVariant::UInt, "max_email_length", n);
    nn->setDescription(BTranslation::translate("initSettings", "Maximum length of the e-mail field.\n"
                                               "The default is 150."));
    nn = new BSettingsNode(QVariant::UInt, "max_name_length", n);
    nn->setDescription(BTranslation::translate("initSettings", "Maximum length of the name field.\n"
                                               "The default is 50."));
    nn = new BSettingsNode(QVariant::UInt, "max_subject_length", n);
    nn->setDescription(BTranslation::translate("initSettings", "Maximum length of the subject field.\n"
                                               "The default is 150."));
    nn = new BSettingsNode(QVariant::UInt, "max_text_length", n);
    nn->setDescription(BTranslation::translate("initSettings", "Maximum length of the text field.\n"
                                               "The default is 15000."));
    nn = new BSettingsNode(QVariant::UInt, "max_password_length", n);
    nn->setDescription(BTranslation::translate("initSettings", "Maximum length of the password field.\n"
                                               "The default is 150."));
    nn = new BSettingsNode(QVariant::UInt, "max_file_size", n);
    nn->setDescription(BTranslation::translate("initSettings", "Maximum attached file size (in bytes).\n"
                                               "The default is 10485760 (10 MB)."));
    nn = new BSettingsNode(QVariant::UInt, "max_file_count", n);
    nn->setDescription(BTranslation::translate("initSettings", "Maximum attached file count.\n"
                                               "The default is 1."));
    nn = new BSettingsNode(QVariant::UInt, "archive_limit", n);
    nn->setDescription(BTranslation::translate("initSettings", "Maximum archived thread count per board.\n"
                                               "The default is 0 (do not archive)."));
    nn = new BSettingsNode(QVariant::UInt, "captcha_quota", n);
    nn->setDescription(BTranslation::translate("initSettings", "Maximum count of extra posts a user may make before "
                                               "solving captcha again.\n"
                                               "The default is 0 (solve captcha every time)."));
    nn = new BSettingsNode(QVariant::String, "launch_date", n);
    BTranslation t = BTranslation::translate("initSettings", "Date and time of first site launch.\n"
                                             "Is used to calculate board speed.\n"
                                             "Format: %1\n"
                                             "By default, the date of creation of application settings file is used.");
    t.setArgument(Tools::InputDateTimeFormat);
    nn->setDescription(t);
    t = BTranslation::translate("initSettings", "MIME types of files allowed for attaching.\n"
                                "Must be separated by commas. Wildcard matching is used.\n"
                                "The default is %1.");
    t.setArgument(AbstractBoard::defaultFileTypes);
    nn = new BSettingsNode(QVariant::String, "supported_file_types", n);
    nn->setDescription(t);
    /*======================================== Site ========================================*/
    n = new BSettingsNode("Site", root);
    nn = new BSettingsNode(QVariant::String, "domain", n);
    nn->setDescription(BTranslation::translate("initSettings", "Site domain name.\n"
                                               "Example: mysite.com"));
    nn = new BSettingsNode(QVariant::String, "protocol", n);
    nn->setDescription(BTranslation::translate("initSettings", "Site protocol.\n"
                                               "Either http or https"));
    nn = new BSettingsNode(QVariant::String, "path_prefix", n);
    nn->setDescription(BTranslation::translate("initSettings", "Global site prefix.\n"
                                               "For example, if prefix is board/, the resulting URL will start with "
                                               "your-site.com/board/."));
    nn = new BSettingsNode(QVariant::String, "tripcode_salt", n);
    nn->setDescription(BTranslation::translate("initSettings", "A salt used to generate tripcodes from hashpasses."));
    nn = new BSettingsNode(QVariant::String, "ssl_proxy_query", n);
    nn->setDescription(BTranslation::translate("initSettings", "Query used to proxy non-SSL links inside iframes.\n"
                                               "Must contain \"%1\" (without quotes) - it is replaced by URL."));
    nn = new BSettingsNode(QVariant::String, "youtube_api_key", n);
    nn->setDescription(BTranslation::translate("initSettings", "The key required to access YouTube API.\n"
                                               "It will appear in HTML."));
    nn = new BSettingsNode(QVariant::String, "file_link_dl_proxy", n);
    nn->setDescription(BTranslation::translate("initSettings", "Proxy used to download files attached as links.\n"
                                               "May be useful when your server is under a firewall.\n"
                                               "If no protocol is specified, it defaults to http.\n"
                                               "If port is not specified, it defaults to 8080.\n"
                                               "See CURLOPT_PROXY for detals.\n"
                                               "Example: 123.234.56.78:8080"));
    nn = new BSettingsNode(QVariant::String, "file_link_dl_proxy_userpwd", n);
    nn->setDescription(BTranslation::translate("initSettings", "Username/password for proxy used to download files "
                                               "attached as links.\n"
                                               "May be useful when your server is under a firewall.\n"
                                               "See CURLOPT_PROXYUSERPWD for detals.\n"
                                               "Example: user:passw0rd"));
    /*======================================== Captcha ========================================*/
    n = new BSettingsNode("Captcha", root); //NOTE: Yep, it must be here.
    /*======================================== System ========================================*/
    n = new BSettingsNode("System", root);
    nn = new BSettingsNode(QVariant::Bool, "use_x_real_ip", n);
    nn->setDescription(BTranslation::translate("initSettings", "Determines if HTTP_X_REAL_IP header is used to "
                                               "get user IP address.\n"
                                               "Otherwise the address is taken directly from the socket.\n"
                                               "Useful when a proxy HTTP server is used (for example, nginx).\n"
                                               "The server must be configured properly.\n"
                                               "The default is false."));
    nn = new BSettingsNode(QVariant::Int, "logging_mode", n);
    nn->setUserSetFunction(&setLoggingMode);
    nn->setDescription(BTranslation::translate("initSettings", "Logging mode. Possible values:\n"
                                               "  0 or less - don't log\n"
                                               "  1 - log to console only\n"
                                               "  2 - log to file only\n"
                                               "  3 and more - log to console and file\n"
                                               "  The default is 2."));
    nn = new BSettingsNode(QVariant::String, "convert_command", n);
    nn->setDescription(BTranslation::translate("initSettings", "convert utility command from ImageMagick (possibly "
                                               "full path).\n"
                                               "The default is convert (UNIX) or convert.exe (Windows)."));
    nn = new BSettingsNode(QVariant::String, "ffmpeg_command", n);
    nn->setDescription(BTranslation::translate("initSettings", "ffmpeg utility command (possibly full path).\n"
                                               "The default is ffmpeg (UNIX) or ffmpeg.exe (Windows)."));
    nn = new BSettingsNode(QVariant::String, "ffprobe_command", n);
    nn->setDescription(BTranslation::translate("initSettings", "ffprobe utility command (possibly full path).\n"
                                               "The default is ffprobe (UNIX) or ffprobe.exe (Windows)."));
    nn = new BSettingsNode(QVariant::String, "file_command", n);
    nn->setDescription(BTranslation::translate("initSettings", "file utility command (possibly full path).\n"
                                               "The default is file (UNIX) or file.exe (Windows)."));
    nn = new BSettingsNode(QVariant::Bool, "use_external_libmagic", n);
    nn->setDescription(BTranslation::translate("initSettings", "Determines if an external file utility is used to "
                                               "identify MIME type.\n"
                                               "By default MIME types are identified internally using libmagic.\n"
                                               "Set this option to false if libmagic crashes the application.\n"
                                               "Calling external file utility is MUCH slower."));
    nn = new BSettingsNode(QVariant::String, "logging_skip_ip", n);
    nn->setUserSetFunction(&setLoggingSkipIp);
    nn->setDescription(BTranslation::translate("initSettings", "List of IP addresses which are not logged.\n"
                                               "IP's are represented as ranges and are separated by commas.\n"
                                               "Example: 127.0.0.1,192.168.0.1-192.168.0.255"));
    nn = new BSettingsNode(QVariant::UInt, "max_render_threads", n);
    nn->setDescription(BTranslation::translate("initSettings", "Determines how many threads may be used "
                                               "simultaneously to render pages.\n"
                                               "The default is QThread::idealThreadCount()"));
    nn = new BSettingsNode("Proxy", n);
    BSettingsNode *nnn = new BSettingsNode(QVariant::Bool, "detect_real_ip", nn);
    nnn->setDescription(BTranslation::translate("initSettings", "Determines if real IP of a client is detected.\n"
                                                "Otherwise the address may be an address of a proxy server.\n"
                                                "Works for non-transparent proxies only (X-Forwarded-For, "
                                                "X-Client-IP).\n"
                                                "The default is true."));
    nn = new BSettingsNode(QVariant::String, "time_zone_offset", n);
    nn->setDescription(BTranslation::translate("initSettings", "Time zone offset in minutes.\n"
                                               "The value must be between -720 and 840.\n"
                                               "The default is -1000 (no offset)."));
    /*======================================== Cache ========================================*/
    n = new BSettingsNode("Cache", root);
    foreach (const QString &s, Cache::availableCacheNames()) {
        nn = new BSettingsNode(s, n);
        BSettingsNode *nnn = new BSettingsNode(QVariant::Int, "max_size", nn);
        nnn->setUserSetFunction(&setMaxCacheSize);
        BTranslation t = BTranslation::translate("initSettings", "Maximum cache size (in bytes or in units).\n"
                                                 "The default value is %1.");
        t.setArgument(QString::number(Cache::defaultCacheSize(s)));
        nnn->setDescription(t);
    }
    BTerminal::setRootSettingsNode(root);
}

void initTerminal()
{
    BTerminal::setMode(BTerminal::StandardMode);
    initCommands();
    initSettings();
}

QString logFileName()
{
    QString fn = BCoreApplication::location(BCoreApplication::DataPath, BCoreApplication::UserResource) + "/logs/";
    fn += QDateTime::currentDateTime().toString(LogFileDateTimeFormat) + ".txt";
    return fn;
}

QString msecsToString(qint64 msecs)
{
    QString days = QString::number(msecs / (24 * BeQt::Hour));
    msecs %= (24 * BeQt::Hour);
    QString hours = QString::number(msecs / BeQt::Hour);
    hours.prepend(QString().fill('0', 2 - hours.length()));
    msecs %= BeQt::Hour;
    QString minutes = QString::number(msecs / BeQt::Minute);
    minutes.prepend(QString().fill('0', 2 - minutes.length()));
    msecs %= BeQt::Minute;
    QString seconds = QString::number(msecs / BeQt::Second);
    seconds.prepend(QString().fill('0', 2 - seconds.length()));
    return days + " " + translate("msecsToString", "day(s)") + " " + hours + ":" + minutes + ":" + seconds;
}

bool setDefaultThreadPassword(const BSettingsNode *, const QVariant &value)
{
    QString s = !value.isNull() ? value.toString() :
                                  bReadLineSecure(translate("setDefaultThreadPassword", "Enter password:") + " ");
    if (s.isEmpty())
        return false;
    QByteArray password = QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1);
    SettingsLocker()->setValue("Board/default_thread_password", password);
    return true;
}

bool setLoggingMode(const BSettingsNode *, const QVariant &v)
{
    QString s = !v.isNull() ? v.toString() : bReadLine(translate("setLoggingMode", "Enter logging mode:") + " ");
    if (s.isEmpty())
        return false;
    bool ok = false;
    int m = s.toInt(&ok);
    if (!ok)
        return false;
    SettingsLocker()->setValue("System/logging_mode", m);
    updateLoggingMode();
    return true;
}

bool setLoggingSkipIp(const BSettingsNode *, const QVariant &v)
{
    QString s = !v.isNull() ? v.toString() : bReadLine(translate("setLoggingSkipIp", "Enter skipped IP's:") + " ");
    if (s.isEmpty())
        return false;
    SettingsLocker()->setValue("System/logging_skip_ip", s);
    Tools::resetLoggingSkipIps();
    return true;
}

bool setMaxCacheSize(const BSettingsNode *node, const QVariant &value)
{
    if (!node) {
        bWriteLine(translate("setMaxCacheSize", "Internal error"));
        return false;
    }
    BSettingsNode *p = node->parent();
    if (!p) {
        bWriteLine(translate("setMaxCacheSize", "Internal error"));
        return false;
    }
    QString s = !value.isNull() ? value.toString() : bReadLine(translate("setMaxCacheSize", "Enter size:") + " ");
    if (s.isEmpty()) {
        bWriteLine(translate("setMaxCacheSize", "Invalid value"));
        return false;
    }
    bool ok = false;
    int sz = s.toInt(&ok);
    if (!ok) {
        bWriteLine(translate("setMaxCacheSize", "Invalid value"));
        return false;
    }
    QString err;
    if (!Cache::setMaxCacheSize(p->key(), sz, &err)) {
        bWriteLine(err);
        return false;
    }
    SettingsLocker()->setValue("Cache/" + p->key() + "/max_size", sz);
    return true;
}

bool showDefaultThreadPassword(const BSettingsNode *, const QVariant &)
{
    bWriteLine(translate("showDefaultThreadPassword",
                         "Password is stored as a hash (SHA-1). You may see it in the .conf file."));
    return true;
}

void updateLoggingMode()
{
    int m = SettingsLocker()->value("System/logging_mode", 2).toInt();
    if (m <= 0) {
        bLogger->setLogToConsoleEnabled(false);
        bLogger->setLogToFileEnabled(false);
    } else if (1 == m) {
        bLogger->setLogToConsoleEnabled(true);
        bLogger->setLogToFileEnabled(false);
    } else if (2 == m) {
        bLogger->setLogToConsoleEnabled(false);
        bLogger->setLogToFileEnabled(true);
    } else if (m >= 3) {
        bLogger->setLogToConsoleEnabled(true);
        bLogger->setLogToFileEnabled(true);
    }
}
