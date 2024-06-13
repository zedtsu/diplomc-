#include "tools.h"

#include "board/abstractboard.h"
#include "cache.h"
#include "controller/ban.h"
#include "controller/board.h"
#include "controller/error.h"
#include "controller/notfound.h"
#include "database.h"
#include "settingslocker.h"
#include "translator.h"

#include <BCoreApplication>
#include <BDirTools>
#include <BeQt>
#include <BLogger>
#include <BTextTools>

#include <QByteArray>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QList>
#include <QLocale>
#include <QMap>
#include <QMutex>
#include <QPair>
#include <QRegExp>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>
#include <QTextCodec>
#include <QThread>
#include <QTime>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <QMimeDatabase>
#include <QMimeType>
#endif

#include <cppcms/http_cookie.h>
#include <cppcms/http_file.h>
#include <cppcms/http_request.h>
#include <cppcms/json.h>

#include <magic.h>

#include <taglib/attachedpictureframe.h>
#include <taglib/fileref.h>
#include <taglib/id3v2tag.h>
#include <taglib/mpegfile.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>

#include <curlpp/cURLpp.hpp>
#include <curlpp/Easy.hpp>
#include <curlpp/Options.hpp>

#include <cmath>
#include <istream>
#include <list>
#include <locale>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <string>

namespace Tools
{

/*static QString ipStr(unsigned int ip)
{
    QString s;
    s += QString::number(ip / (256 * 256 * 256)) + ".";
    ip %= (256 * 256 * 256);
    s += QString::number(ip / (256 * 256)) + ".";
    ip %= (256 * 256);
    s += QString::number(ip / 256) + ".";
    s += QString::number(ip % 256);
    return s;
}*/

IpRange::IpRange(const QString &text, const QChar &separator)
{
    start = 0;
    end = 0;
    QStringList sl = text.split(separator);
    if (sl.size() > 2)
        return;
    bool ok = false;
    start = Tools::ipNum(sl.first(), &ok);
    if (!ok)
        return;
    if (sl.size() == 2) {
        end = Tools::ipNum(sl.at(1), &ok);
        if (!ok) {
            start = 0;
            return;
        }
    } else {
        end = start;
    }
}

IpRange::IpRange(const QStringList &sl, int startIndex, int endIndex, bool num)
{
    start = 0;
    end = 0;
    if (startIndex < 0 || endIndex < 0 || startIndex >= sl.size() || endIndex >= sl.size())
        return;
    bool ok = false;
    if (num) {
        start = sl.at(startIndex).toUInt(&ok);
        if (!ok)
            return;
        end = sl.at(endIndex).toUInt(&ok);
    } else {
        start = Tools::ipNum(sl.at(startIndex), &ok);
        if (!ok)
            return;
        end = Tools::ipNum(sl.at(endIndex), &ok);
    }
    if (!ok) {
        start = 0;
        return;
    }
}

void IpRange::clear()
{
    start = 0;
    end = 0;
}

bool IpRange::in(unsigned int ip) const
{
    if (!ip || !isValid())
        return false;
    return ip >= start && ip <= end;
}

bool IpRange::in(const QString &ip) const
{
    return in(Tools::ipNum(ip));
}

bool IpRange::isValid() const
{
    return start && end;
}

bool IpRange::operator <(const IpRange &other) const
{
    return start < other.start;
}

IpBanInfo::IpBanInfo(const QStringList &sl) :
    range(!sl.isEmpty() ? sl.first() : QString())
{
    if (sl.size() != 2) {
        range.clear();
        return;
    }
    bool ok = false;
    level = sl.at(1).toInt(&ok);
    if (!ok)
        range.clear();
}

bool IpBanInfo::isValid() const
{
    return range.isValid();
}

static QMutex cityNameMutex(QMutex::Recursive);
static QMutex countryCodeMutex(QMutex::Recursive);
static QMutex countryNameMutex(QMutex::Recursive);
static QMap<QString, double> ddos;
static const qint64 DdosBanPeriod = BeQt::Minute;
static const qint64 DdosClearPeriod = BeQt::Hour;
static const double DdosLimit = 10000.0;
static QMutex ddosMutex;
static const qint64 DdosPeriod = 10 * BeQt::Second;
static QElapsedTimer ddosTimer;
static bool ddosTimerStarted = false;
static QMap<QString, QElapsedTimer *> ddosWait;
static QMutex ddosWaitMutex;
static QElapsedTimer ddosWaitTimer;
static bool ddosWaitTimerStarted = false;
static QList<IpRange> loggingSkipIps;
static QMutex loggingSkipIpsMutex(QMutex::Recursive);
static unsigned int renderThreads = 0;
static QMutex renderThreadsMutex;
static QMutex storagePathMutex(QMutex::Recursive);
static QMutex timezoneMutex(QMutex::Recursive);

static QTime time(int msecs)
{
    int h = msecs / BeQt::Hour;
    msecs %= BeQt::Hour;
    int m = msecs / BeQt::Minute;
    msecs %= BeQt::Minute;
    int s = msecs / BeQt::Second;
    return QTime(h, m, s, msecs % BeQt::Second);
}

QStringList acceptedExternalBoards()
{
    QString fn = BDirTools::findResource("res/echo.txt", BDirTools::UserOnly);
    return BDirTools::readTextFile(fn, "UTF-8").split(QRegExp("\\r?\\n+"), QString::SkipEmptyParts);
}

AudioTags audioTags(const QString &fileName)
{
    if (fileName.isEmpty())
        return AudioTags();
    AudioTags a;
    TagLib::FileRef f(toStd(fileName).data());
    if(!f.isNull() && f.tag()) {
        TagLib::Tag *tag = f.tag();
        a.album = TStringToQString(tag->album());
        a.artist = TStringToQString(tag->artist());
        a.title = TStringToQString(tag->title());
        if (tag->year() > 0)
            a.year = QString::number(tag->year());
    }
    QString suff = QFileInfo(fileName).suffix();
    if (!suff.compare("mp3", Qt::CaseInsensitive) || !suff.compare("mpeg", Qt::CaseInsensitive)) {
        TagLib::MPEG::File audioFile(toStd(fileName).data());
        TagLib::ID3v2::Tag *tag = audioFile.ID3v2Tag();
        if (tag) {
            TagLib::ID3v2::FrameList list = tag->frameListMap()["APIC"];
            if (!list.isEmpty()) {
                TagLib::ID3v2::AttachedPictureFrame *pic =
                        dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(list.front());
                if (pic)
                    a.cover.loadFromData((const uchar *) pic->picture().data(), pic->picture().size());
            }
        }
    }
    return a;
}

QString captchaQuotaFile()
{
    return BCoreApplication::location("storage", BCoreApplication::UserResource) + "/captcha-quota.dat";
}

bool captchaEnabled(const QString &boardName)
{
    SettingsLocker s;
    return s->value("Board/captcha_enabled", true).toBool()
            && (boardName.isEmpty() || s->value("Board/" + boardName + "/captcha_enabled", true).toBool());
}

QString cookieValue(const cppcms::http::request &req, const QString &name)
{
    if (name.isEmpty())
        return "";
    QByteArray ba = const_cast<cppcms::http::request *>(&req)->cookie_by_name(toStd(name)).value().data();
    return QUrl::fromPercentEncoding(ba);
}

QString customContent(const QString &prefix, const QLocale &l)
{
    QString *s = Cache::customContent(prefix, l);
    if (!s) {
        QString path = BDirTools::findResource("custom/" + prefix, BDirTools::UserOnly);
        if (path.isEmpty())
            return QString();
        QString fn = BDirTools::localeBasedFileName(path + "/content.html", l);
        if (fn.isEmpty())
            return QString();
        s = new QString(BDirTools::readTextFile(fn, "UTF-8"));
        if (!Cache::cacheCustomContent(prefix, l, s)) {
            QString ss = *s;
            delete s;
            return ss;
        }
    }
    return *s;
}

QList<CustomLinkInfo> customLinks(const QLocale &l)
{
    QList<CustomLinkInfo> *list = Cache::customLinks(l);
    if (!list) {
        QString path = BDirTools::findResource("res", BDirTools::UserOnly);
        if (path.isEmpty())
            return QList<CustomLinkInfo>();
        QString fn = BDirTools::localeBasedFileName(path + "/custom_links.txt", l);
        if (fn.isEmpty())
            return QList<CustomLinkInfo>();
        list = new QList<CustomLinkInfo>;
        QStringList sl = BDirTools::readTextFile(fn, "UTF-8").split(QRegExp("\\r?\\n+"), QString::KeepEmptyParts);
        foreach (const QString &s, sl) {
            QStringList sll = BTextTools::splitCommand(s);
            if (sll.size() < 2)
                continue;
            if (sll.first().isEmpty() || sll.at(1).isEmpty())
                continue;
            CustomLinkInfo info;
            info.text = sll.first();
            info.url = sll.at(1);
            if (sll.size() > 2)
                info.imgUrl = sll.at(2);
            if (sll.size() > 3)
                info.target = sll.at(3);
            *list << info;
        }
        if (!Cache::cacheCustomLinks(l, list)) {
            QList<CustomLinkInfo> llist = *list;
            delete list;
            return llist;
        }
    }
    return *list;
}

QDateTime dateTime(const QDateTime &dt, const cppcms::http::request &req)
{
    QString s = cookieValue(req, "time");
    int def = SettingsLocker()->value("System/time_zone_offset", -1000).toInt();
    if (s.isEmpty() || s.compare("local", Qt::CaseInsensitive))
        return localDateTime(dt, def);
    return localDateTime(dt, timeZoneMinutesOffset(req, def));
}

bool ddosTest(const cppcms::application &app, double weight, double previousWeight)
{
    if (weight <= 0.0)
        return true;
    QString ip = userIp(const_cast<cppcms::application *>(&app)->request());
    if (ip.isEmpty())
        return true;
    bool b = false;
    ddosWaitMutex.lock();
    if (!ddosWaitTimerStarted) {
        ddosWaitTimerStarted = true;
        ddosWaitTimer.start();
    }
    if (ddosWaitTimer.elapsed() >= DdosClearPeriod) {
        ddosWaitTimer.restart();
        foreach (QElapsedTimer *etmr, ddosWait)
            delete etmr;
        ddosWait.clear();
    }
    QElapsedTimer *etmr = ddosWait.value(ip);
    if (etmr) {
        if (etmr->elapsed() >= DdosBanPeriod)
            delete ddosWait.take(ip);
        else
            b = true;
    }
    ddosWaitMutex.unlock();
    QMutexLocker lock(&ddosMutex);
    if (!ddosTimerStarted) {
        ddosTimerStarted = true;
        ddosTimer.start();
    }
    if (ddosTimer.elapsed() >= (DdosPeriod)) {
        ddosTimer.restart();
        ddos.clear();
    }
    double &x = ddos[ip];
    x += weight;
    if (previousWeight > 0.0)
        x -= previousWeight;
    if (!b && x >= DdosLimit) {
        ddosWaitMutex.lock();
        QElapsedTimer *etmr = new QElapsedTimer;
        etmr->start();
        ddosWait.insert(ip, etmr);
        ddosWaitMutex.unlock();
    }
    b = b || (x >= DdosLimit);
    return !b;
}

QString externalLinkRegexpPattern()
{
    init_once(QString, pattern, QString()) {
        QString schema = "https?:\\/\\/|ftp:\\/\\/";
        QString ip = "(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.){3}"
                "([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])";
        QString hostname = "([\\w\\.\\-]+)\\.([a-z]{2,17}\\.?)";
        QString port = ":\\d+";
        QString path = "(\\/[\\w\\.\\-\\!\\?\\=\\+#~&%:\\,\\(\\)]*)*\\/?";
        pattern = "(" + schema + ")?(" + hostname + "|" + ip + ")(" + port + ")?" + path + "(?!\\S)";
    }
    return pattern;
}

bool externalLinkRootZoneExists(const QString &zoneName)
{
    typedef QSet<QString> StringSet;
    init_once(StringSet, rootZones, StringSet()) {
        QString fn = BDirTools::findResource("res/root-zones.txt", BDirTools::GlobalOnly);
        QStringList list = BDirTools::readTextFile(fn, "UTF-8").split(QRegExp("\\r?\\n+"), QString::SkipEmptyParts);
        rootZones = list.toSet();
    }
    return !zoneName.isEmpty() && rootZones.contains(zoneName);
}

QString flagName(const QString &countryCode)
{
    if (countryCode.length() != 2)
        return "";
    QString fn = BDirTools::findResource("static/img/flag/" + countryCode.toUpper() + ".png");
    return !fn.isEmpty() ? QFileInfo(fn).fileName() : QString();
}

QVariant fromJson(const cppcms::json::value &v)
{
    try {
        switch (v.type()) {
        case cppcms::json::is_array: {
            QVariantList l;
            foreach (const cppcms::json::value &vv, v.array())
                l << fromJson(vv);
            return l;
        }
        case cppcms::json::is_boolean: {
            return v.boolean();
        }
        case cppcms::json::is_number: {
            return v.number();
        }
        case cppcms::json::is_object: {
            QVariantMap m;
            const cppcms::json::object &o = v.object();
            for (cppcms::json::object::const_iterator i = o.begin(); i != o.end(); ++i)
                m.insert(fromStd(i->first), fromJson(i->second));
            return m;
        }
        case cppcms::json::is_string: {
            return fromStd(v.str());
        }
        default:
            return QVariant();
        }
    } catch (const std::exception &e) {
        log("Tools::fromJson", e);
        return QVariant();
    }
}

QLocale fromStd(const std::locale &l)
{
    return QLocale(fromStd(l.name()).split('.').first());
}

QString fromStd(const std::string &s)
{
    return QString::fromLocal8Bit(s.data());
}

QStringList fromStd(const std::list<std::string> &sl)
{
    QStringList list;
    foreach (const std::string &s, sl)
        list << fromStd(s);
    return list;
}

GetParameters getParameters(const cppcms::http::request &request)
{
    PostParameters m;
    cppcms::http::request::form_type data = const_cast<cppcms::http::request *>(&request)->get();
    for (cppcms::http::request::form_type::iterator i = data.begin(); i != data.end(); ++i)
        m.insert(fromStd(i->first), fromStd(i->second));
    return m;
}

QByteArray hashpass(const cppcms::http::request &req)
{
    return toHashpass(hashpassString(req));
}

QString hashpassString(const cppcms::http::request &req)
{
    return cookieValue(req, "hashpass");
}

int ipBanLevel(const QString &ip)
{
    bool ok = false;
    int n = ipNum(ip, &ok);
    if (!ok)
        return 0;
    Cache::IpBanInfoList *list = Cache::ipBanInfoList();
    int level = 0;
    if (!list) {
        QString path = BDirTools::findResource("res/ip_ban.txt", BDirTools::UserOnly);
        if (path.isEmpty())
            return 0;
        QStringList sl = BDirTools::readTextFile(path, "UTF-8").split(QRegExp("\\r?\\n+"), QString::SkipEmptyParts);
        list = new Cache::IpBanInfoList;
        foreach (const QString &s, sl) {
            IpBanInfo inf(s.split(' '));
            if (!inf.isValid())
                continue;
            if (inf.range.in(n))
                level = inf.level;
            *list << inf;
        }
        if (!Cache::cacheIpBanInfoList(list))
            delete list;
    } else {
        foreach (const IpBanInfo &inf, *list) {
            if (inf.range.in(n)) {
                level = inf.level;
                break;
            }
        }
    }
    return level;
}

int ipBanLevel(const cppcms::http::request &req)
{
    return ipBanLevel(userIp(req));
}

bool isAudioType(const QString &mimeType)
{
    typedef QSet<QString> StringSet;
    init_once(StringSet, types, StringSet()) {
        types.insert("audio/mpeg");
        types.insert("audio/ogg");
        types.insert("audio/wav");
    }
    return types.contains(mimeType);
}

bool isImageType(const QString &mimeType)
{
    typedef QSet<QString> StringSet;
    init_once(StringSet, types, StringSet()) {
        types.insert("image/gif");
        types.insert("image/jpeg");
        types.insert("image/png");
    }
    return types.contains(mimeType);
}

IsMobile isMobile(const cppcms::http::request &req)
{
    static const QRegExp AmazonPhone("(?=.*\\bAndroid\\b)(?=.*\\bSD4930UR\\b)");
    static const QRegExp AmazonTablet("(?=.*\\bAndroid\\b)(?=.*\\b(?:KFOT|KFTT|KFJWI|KFJWA|KFSOWI|KFTHWI|KFTHWA|"
                                      "KFAPWI|KFAPWA|KFARWI|KFASWI|KFSAWI|KFSAWA)\\b)");
    static const QRegExp AndroidPhone("(?=.*\\bAndroid\\b)(?=.*\\bMobile\\b)");
    static const QRegExp AndroidTablet("Android");
    static const QRegExp AppleIPod("iPod");
    static const QRegExp ApplePhone("iPhone");
    static const QRegExp AppleTablet("iPad");
    static const QRegExp OtherBlackberry("BlackBerry");
    static const QRegExp OtherBlackberry10("BB10");
    static const QRegExp OtherFirefox("(?=.*\\bFirefox\\b)(?=.*\\bMobile\\b)");
    static const QRegExp OtherOpera("Opera Mini");
    static const QRegExp SevenInch("(?:Nexus 7|BNTV250|Kindle Fire|Silk|GT-P1000)");
    static const QRegExp WindowsPhone("IEMobile");
    static const QRegExp WindowsTablet("(?=.*\\bWindows\\b)(?=.*\\bARM\\b)");
    QString ua = Tools::fromStd(const_cast<cppcms::http::request *>(&req)->http_user_agent());
    IsMobile im;
    im.apple.phone = ua.contains(ApplePhone);
    im.apple.ipod = ua.contains(AppleIPod);
    im.apple.tablet = !ua.contains(ApplePhone) && ua.contains(AppleTablet);
    im.apple.device = ua.contains(ApplePhone) || ua.contains(AppleIPod) || ua.contains(AppleTablet);
    im.amazon.phone = ua.contains(AmazonPhone);
    im.amazon.tablet = !ua.contains(AmazonPhone) && ua.contains(AmazonTablet);
    im.amazon.device = ua.contains(AmazonPhone) || ua.contains(AmazonTablet);

    im.android.phone = ua.contains(AmazonPhone) || ua.contains(AndroidPhone);
    im.android.tablet = !ua.contains(AmazonPhone) && !ua.contains(AndroidPhone)
            && (ua.contains(AmazonTablet) || ua.contains(AndroidTablet));
    im.android.device = ua.contains(AmazonPhone) || ua.contains(AmazonTablet) || ua.contains(AndroidPhone)
            || ua.contains(AndroidTablet);
    im.windows.phone = ua.contains(WindowsPhone);
    im.windows.tablet = ua.contains(WindowsTablet);
    im.windows.device = ua.contains(WindowsPhone) || ua.contains(WindowsTablet);
    im.other.blackberry = ua.contains(OtherBlackberry);
    im.other.blackberry10 = ua.contains(OtherBlackberry10);
    im.other.opera = ua.contains(OtherOpera);
    im.other.firefox = ua.contains(OtherFirefox);
    im.other.device = ua.contains(OtherBlackberry) || ua.contains(OtherBlackberry10) || ua.contains(OtherOpera)
            || ua.contains(OtherFirefox);
    im.sevenInch = ua.contains(SevenInch);
    im.any = im.apple.device || im.android.device || im.windows.device || im.other.device || im.sevenInch;
    im.phone = im.apple.phone || im.android.phone || im.windows.phone;
    im.tablet = im.apple.tablet || im.android.tablet || im.windows.tablet;
    return im;
}

unsigned int ipNum(const QString &ip, bool *ok)
{
    QStringList sl = ip.split('.');
    if (sl.size() != 4)
        return bRet(ok, false, 0);
    bool b = false;
    unsigned int n = sl.last().toUInt(&b);
    if (!b)
        return bRet(ok, false, 0);
    n += 256 * sl.at(2).toUInt(&b);
    if (!b)
        return bRet(ok, false, 0);
    n += 256 * 256 * sl.at(1).toUInt(&b);
    if (!b)
        return bRet(ok, false, 0);
    n += 256 * 256 * 256 * sl.first().toUInt(&b);
    if (!b || !n)
        return bRet(ok, false, 0);
    return bRet(ok, true, n);
}

bool isSpecialThumbName(const QString &tn)
{
    return isAudioType(tn) || isImageType(tn) || isVideoType(tn);
}

bool isVideoType(const QString &mimeType)
{
    typedef QSet<QString> StringSet;
    init_once(StringSet, types, StringSet()) {
        types.insert("video/mp4");
        types.insert("video/ogg");
        types.insert("video/webm");
    }
    return types.contains(mimeType);
}

QString langName(const QString &id)
{
    typedef QMap<QString, QString> StringMap;
    init_once(StringMap, map, StringMap()) {
        QString fn = BDirTools::findResource("res/lang_name_map.txt");
        QStringList sl = BDirTools::readTextFile(fn, "UTF-8").split(QRegExp("\\r?\\n+"), QString::KeepEmptyParts);
        QSet<QString> supported = supportedCodeLanguages().toSet();
        foreach (const QString &s, sl) {
            QStringList sll = s.split(' ');
            if (sll.size() < 1)
                continue;
            if (sll.first().isEmpty() || !supported.contains(sll.first()))
                continue;
            QString name = QStringList(sll.mid(1)).join(" ");
            map.insert(sll.first(), name);
        }
    }
    return map.value(id);
}

QDateTime localDateTime(const QDateTime &dt, int offsetMinutes)
{
    static const int MaxMsecs = 24 * BeQt::Hour;
    if (offsetMinutes < -720 || offsetMinutes > 840)
        return dt.toLocalTime();
    QDateTime ndt = dt.toUTC();
    QTime t = ndt.time();
    int msecs = t.hour() * BeQt::Hour + t.minute() * BeQt::Minute + t.second() * BeQt::Second + t.msec();
    int msecsOffset = offsetMinutes * BeQt::Minute;
    msecs += msecsOffset;
    if (msecs < 0) {
        ndt.setDate(ndt.date().addDays(-1));
        ndt.setTime(time(msecs + MaxMsecs));
    } else if (msecs >= MaxMsecs) {
        ndt.setDate(ndt.date().addDays(1));
        ndt.setTime(time(msecs - MaxMsecs));
    } else {
        ndt.setTime(time(msecs));
    }
    return ndt;
}

QLocale locale(const cppcms::http::request &req, const QLocale &defaultLocale)
{
    QLocale l(cookieValue(req, "locale"));
    if (QLocale::c() == l)
        l = QLocale(Database::geolocationInfo(req).countryCode);
    return (QLocale::c() == l) ? defaultLocale : l;
}

void log(const cppcms::application &app, const QString &action, const QString &state, const QString &target)
{
    log(const_cast<cppcms::application *>(&app)->request(), action, state, target);
}

void log(const cppcms::http::request &req, const QString &action, const QString &state, const QString &target)
{
    do_once(init)
        resetLoggingSkipIps();
    QString ip = userIp(req);
    int n = ipNum(ip);
    QMutexLocker locker(&loggingSkipIpsMutex);
    foreach (const IpRange &r, loggingSkipIps) {
        if (r.in(n))
            return;
    }
    locker.unlock();
    bLog("[" + ip + "] [" + action + "] [" + state + "]" + (!target.isEmpty() ? (" " + target) : QString()));
}

void log(const char *where, const std::exception &e)
{
    QString s = "[" + QString::fromLatin1(where) + "] [" + QString::fromLatin1(typeid(e).name()) + "]"
            + Tools::fromStd(e.what());
    qDebug() << s;
    bLog(s);
}

unsigned int maxInfo(MaxInfo m, const QString &boardName)
{
    typedef QMap< MaxInfo, QPair<QString, uint> > MaxMap;
    init_once(MaxMap, map, MaxMap()) {
        map.insert(MaxEmailFieldLength, qMakePair(QString("max_email_length"), uint(150)));
        map.insert(MaxNameFieldLength, qMakePair(QString("max_name_length"), uint(50)));
        map.insert(MaxSubjectFieldLength, qMakePair(QString("max_subject_length"), uint(150)));
        map.insert(MaxTextFieldLength, qMakePair(QString("max_text_length"), uint(15000)));
        map.insert(MaxPasswordFieldLength, qMakePair(QString("max_password_length"), uint(150)));
        map.insert(MaxFileCount, qMakePair(QString("max_file_count"), uint(1)));
        map.insert(MaxFileSize, qMakePair(QString("max_file_size"), uint(10)));
        map.insert(MaxLastPosts, qMakePair(QString("max_last_posts"), uint(3)));
    }
    if (!map.contains(m))
        return 0;
    QPair<QString, uint> p = map.value(m);
    SettingsLocker s;
    if (boardName.isEmpty())
        return s->value("Board/" + p.first, p.second).toUInt();
    else
        return s->value("Board/" + boardName + "/" + p.first, s->value("Board/" + p.first, p.second)).toUInt();
}

QString mimeType(const QByteArray &data, bool *ok)
{
#if defined(Q_OS_WIN)
    static const QString FileDefault = "file.exe";
#elif defined(Q_OS_UNIX)
    static const QString FileDefault = "file";
#endif
    if (data.isEmpty())
        return bRet(ok, false, QString());
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    QMimeDatabase db;
    QString name = db.mimeTypeForData(data).name();
    if (!name.isEmpty() && "application/octet-stream" != name)
        return bRet(ok, true, name);
#endif
    SettingsLocker sl;
    if (sl->value("System/use_external_libmagic", false).toBool()) {
        QString file = sl->value("System/file_command", FileDefault).toString();
        QTemporaryFile f;
        if (!f.open())
            return bRet(ok, false, QString());
        f.write(data);
        f.close();
        if (f.error() != QFile::NoError)
            return bRet(ok, false, QString());
        QString out;
        QStringList args = QStringList() << "--brief" << "--mime-type" << f.fileName();
        if (BeQt::execProcess(QFileInfo(f).path(), file, args, BeQt::Second, 5 * BeQt::Second, &out))
            return bRet(ok, false, QString());
        out.remove("\r").remove("\n");
        return bRet(ok, !out.isEmpty(), out);
    } else {
        magic_t magicMimePredictor;
        magicMimePredictor = magic_open(MAGIC_MIME_TYPE);
        if (!magicMimePredictor)
            return bRet(ok, false, QString());
        if (magic_load(magicMimePredictor, 0)) {
            magic_close(magicMimePredictor);
            return bRet(ok, false, QString());
        }
        QString result = QString::fromLatin1(magic_buffer(magicMimePredictor, (void *) data.data(), data.size()));
        return bRet(ok, !result.isEmpty(), result);
    }
}

QStringList news(const QLocale &l)
{
    QStringList *sl = Cache::news(l);
    if (!sl) {
        QString path = BDirTools::findResource("news", BDirTools::UserOnly);
        if (path.isEmpty())
            return QStringList();
        QString fn = BDirTools::localeBasedFileName(path + "/news.txt", l);
        if (fn.isEmpty())
            return QStringList();
        sl = new QStringList(BDirTools::readTextFile(fn, "UTF-8").split(QRegExp("\\r?\\n+"), QString::SkipEmptyParts));
        if (!Cache::cacheNews(l, sl)) {
            QStringList sll = *sl;
            delete sl;
            return sll;
        }
    }
    return *sl;
}

FileList postFiles(const cppcms::http::request &request, const PostParameters &params, const QString &boardName,
                   bool *ok, QString *error, const QLocale &l)
{
    FileList list;
    cppcms::http::request::files_type files = const_cast<cppcms::http::request *>(&request)->files();
    TranslatorQt tq(l);
    foreach (int i, bRangeD(0, files.size() - 1)) {
        cppcms::http::file *f = files.at(i).get();
        if (!f) {
            return bRet(ok, false, error, tq.translate("Tools::postFiles", "Internal logic error", "error"),
                        FileList());
        }
        File file;
        std::istream &in = f->data();
        char *buff = new char[f->size()];
        in.read(buff, f->size());
        file.data = QByteArray::fromRawData(buff, f->size());
        file.fileName = QFileInfo(fromStd(f->filename())).fileName();
        file.formFieldName = fromStd(f->name());
        file.mimeType = fromStd(f->mime());
        file.rating = 0;
        QString r = params.value(file.formFieldName + "_rating");
        if ("R-15" == r)
            file.rating = 15;
        else if ("R-18" == r)
            file.rating = 18;
        else if ("R-18G" == r)
            file.rating = 180;
        list << file;
    }
    int maxSize = maxInfo(MaxFileSize, boardName);
    std::string proxy = toStd(SettingsLocker()->value("Site/file_link_dl_proxy").toString());
    std::string proxyUserpwd = toStd(SettingsLocker()->value("Site/file_link_dl_proxy_userpwd").toString());
    foreach (const QString &key, params.keys()) {
        if (!key.startsWith("file_url_"))
            continue;
        File file;
        try {
            curlpp::Cleanup curlppCleanup;
            Q_UNUSED(curlppCleanup)
            QString url = params.value(key);
            curlpp::Easy request;
            request.setOpt(curlpp::options::Url(Tools::toStd(url)));
            request.setOpt(curlpp::options::MaxFileSize(maxSize));
            if (!proxy.empty()) {
                request.setOpt(curlpp::options::Proxy(proxy));
                if (!proxyUserpwd.empty())
                    request.setOpt(curlpp::options::ProxyUserPwd(proxyUserpwd));
            }
            std::ostringstream os;
            os << request;
            std::string s = os.str();
            QByteArray data(s.data(), s.size());
            file.data = data;
        } catch (curlpp::RuntimeError &e) {
            return bRet(ok, false, error, fromStd(e.what()), FileList());
        } catch(curlpp::LogicError &e) {
            return bRet(ok, false, error, fromStd(e.what()), FileList());
        }
        file.fileName = QFileInfo(key.split('/').last()).fileName();
        file.formFieldName = key;
        file.rating = 0;
        QString id = key.mid(9);
        QString r = params.value("file_" + id + "_rating");
        if ("R-15" == r)
            file.rating = 15;
        else if ("R-18" == r)
            file.rating = 18;
        else if ("R-18G" == r)
            file.rating = 180;
        list << file;
    }
    return bRet(ok, true, error, QString(), list);
}

PostParameters postParameters(const cppcms::http::request &request)
{
    PostParameters m;
    cppcms::http::request::form_type data = const_cast<cppcms::http::request *>(&request)->post();
    for (cppcms::http::request::form_type::iterator i = data.begin(); i != data.end(); ++i)
        m.insert(fromStd(i->first), fromStd(i->second));
    return m;
}

cppcms::json::value readJsonValue(const QString &fileName, bool *ok)
{
    bool b = false;
    QString s = BDirTools::readTextFile(fileName, "UTF-8", &b);
    if (!b)
        return bRet(ok, false, cppcms::json::value());
    cppcms::json::value json;
    std::stringstream in(toStd(s));
    if (json.load(in, true))
        return bRet(ok, true, json);
    else
        return bRet(ok, false, cppcms::json::value());
}

void redirect(cppcms::application &app, const QString &path)
{
    if (path.isEmpty()) {
        app.response().set_redirect_header(app.request().http_referer());
    } else {
        QString p = "/" + SettingsLocker()->value("Site/path_prefix").toString() + path;
        app.response().set_redirect_header(toStd(p));
    }
}

void render(cppcms::application &app, const QString &templateName, cppcms::base_content &content)
{
    forever {
        renderThreadsMutex.lock();
        bool b = (renderThreads < SettingsLocker()->value("System/max_render_threads",
                                                          QThread::idealThreadCount()).toUInt());
        if (b)
            ++renderThreads;
        renderThreadsMutex.unlock();
        if (b)
            break;
        BeQt::msleep(1);
    }
    app.render(toStd(templateName), content);
    renderThreadsMutex.lock();
    --renderThreads;
    renderThreadsMutex.unlock();
}

void resetLoggingSkipIps()
{
    QStringList list = SettingsLocker()->value("System/logging_skip_ip").toString().split(QRegExp("\\,\\s*"),
                                                                                          QString::SkipEmptyParts);
    QMutexLocker locker(&loggingSkipIpsMutex);
    loggingSkipIps.clear();
    foreach (const QString &s, list) {
        IpRange r(s);
        if (!r.isValid())
            continue;
        loggingSkipIps << r;
    }
}

QStringList rules(const QString &prefix, const QLocale &l)
{
    QStringList *sl = Cache::rules(l, prefix);
    if (!sl) {
        QString path = BDirTools::findResource(prefix, BDirTools::UserOnly);
        if (path.isEmpty())
            return QStringList();
        QString fn = BDirTools::localeBasedFileName(path + "/rules.txt", l);
        if (fn.isEmpty())
            return QStringList();
        sl = new QStringList(BDirTools::readTextFile(fn, "UTF-8").split(QRegExp("\\r?\\n+"), QString::SkipEmptyParts));
        if (!Cache::cacheRules(prefix, l, sl)) {
            QStringList sll = *sl;
            delete sl;
            return sll;
        }
    }
    return *sl;
}

QString searchIndexFile()
{
    return BCoreApplication::location("storage", BCoreApplication::UserResource) + "/search-index.dat";
}

FriendList siteFriends()
{
    FriendList *list = Cache::friendList();
    if (!list) {
        QString path = BDirTools::findResource("res/friends.txt", BDirTools::UserOnly);
        if (path.isEmpty())
            return FriendList();
        QStringList sl = BDirTools::readTextFile(path, "UTF-8").split(QRegExp("\\r?\\n+"), QString::SkipEmptyParts);
        list = new FriendList;
        foreach (const QString &s, sl) {
            bool ok = false;
            QStringList sll = BTextTools::splitCommand(s, &ok);
            if (!ok || sll.size() < 2 || sll.size() > 3)
                continue;
            Friend f;
            f.url = sll.first();
            f.name = sll.at(1);
            if (sll.size() > 2)
                f.title = sll.last();
            if (f.url.isEmpty() || f.name.isEmpty())
                continue;
            *list << f;
        }
        if (!Cache::cacheFriendList(list)) {
            FriendList llist = *list;
            delete list;
            return llist;
        }
    }
    return *list;
}

QString storagePath()
{
    storagePathMutex.lock();
    init_once(QString, path, QString())
        path = BDirTools::findResource("storage", BDirTools::UserOnly);
    storagePathMutex.unlock();
    return path;
}

QStringList supportedCodeLanguages()
{
    QString srchighlightPath = BDirTools::findResource("srchilite");
    if (srchighlightPath.isEmpty())
        return QStringList();
    QStringList sl = QDir(srchighlightPath).entryList(QStringList() << "*.lang", QDir::Files);
    foreach (int i, bRangeD(0, sl.size() - 1))
        sl[i].remove(".lang");
    int ind = sl.indexOf("cpp");
    if (ind >= 0)
        sl.insert(ind + 1, "c++");
    return sl;
}

int timeZoneMinutesOffset(const cppcms::http::request &req, int defaultOffset)
{
    bool ok = false;
    int offset = cookieValue(req, "timeZoneOffset").toInt(&ok);
    if (!ok || offset < -720 || offset > 840)
        return defaultOffset;
    return offset;
}

QByteArray toHashpass(const QString &s, bool *ok)
{
    if (s.length() != 44)
        return bRet(ok, false, QByteArray());
    QStringList sl = s.split('-');
    if (sl.size() != 5)
        return bRet(ok, false, QByteArray());
    QByteArray ba;
    foreach (const QString &ss, sl) {
        if (ss.length() != 8 || !QRegExp("([0-9a-fA-F]){8}").exactMatch(ss))
            return bRet(ok, false, QByteArray());
        char c[4];
        foreach (int i, bRangeD(0, 3)) {
            bool b = false;
            c[i] = ss.mid(i * 2, 2).toUShort(&b, 16);
            if (!b)
                return bRet(ok, false, QByteArray());
        }
        ba.append(c, 4);
    }
    return bRet(ok, true, ba);
}

cppcms::json::value toJson(const QVariant &v)
{
    try {
        switch (v.type()) {
        case QVariant::List: {
            cppcms::json::array a;
            foreach (const QVariant &vv, v.toList())
                a.push_back(toJson(vv));
            return a;
        }
        case QVariant::Bool: {
            cppcms::json::value vv;
            vv.boolean(v.toBool());
            return vv;
        }
        case QVariant::Double:
        case QVariant::Int:
        case QVariant::UInt:
        case QVariant::LongLong:
        case QVariant::ULongLong: {
            return v.toDouble();
        }
        case QVariant::Map: {
            cppcms::json::object o;
            const QVariantMap &m = v.toMap();
            for (QVariantMap::ConstIterator i = m.begin(); i != m.end(); ++i)
                o.insert(std::pair<std::string, cppcms::json::value>(toStd(i.key()), toJson(i.value())));
            return o;
        }
        case QVariant::String: {
            return toStd(v.toString());
        }
        default:
            return cppcms::json::value();
        }
    } catch (const std::exception &e) {
        log("Tools::toJson", e);
        return cppcms::json::value();
    }
}

Post toPost(const PostParameters &params, const FileList &files)
{
    Post p;
    p.email = params.value("email");
    p.fileHashes = params.value("fileHashes").split(',', QString::SkipEmptyParts);
    p.files = files;
    p.name = params.value("name");
    QString pwd = params.value("password");
    if (pwd.isEmpty())
        pwd = SettingsLocker()->value("Board/default_post_password").toString();
    p.password = QCryptographicHash::hash(pwd.toLocal8Bit(), QCryptographicHash::Sha1);
    p.raw = !params.value("raw").compare("true", Qt::CaseInsensitive);
    p.showTripcode = !params.value("tripcode").compare("true", Qt::CaseInsensitive);
    p.subject = params.value("subject");
    p.text = params.value("text");
    p.draft = !params.value("draft").compare("true", Qt::CaseInsensitive);
    return p;
}

Post toPost(const cppcms::http::request &req, const QString &boardName)
{
    PostParameters params = postParameters(req);
    return toPost(params, postFiles(req, params, boardName));
}

std::locale toStd(const QLocale &l)
{
    return std::locale(toStd(l.name()).data());
}

std::string toStd(const QString &s)
{
    return std::string(s.toLocal8Bit().data());
}

std::list<std::string> toStd(const QStringList &sl)
{
    std::list<std::string> list;
    foreach (const QString &s, sl)
        list.push_back(toStd(s));
    return list;
}

QString toString(const QByteArray &hp, bool *ok)
{
    if (hp.size() != 20)
        return bRet(ok, false, QString());
    QString s;
    foreach (int i, bRangeD(0, hp.size() - 1)) {
        QString c = QString::number(uchar(hp.at(i)), 16);
        if (c.length() < 2)
            c.prepend("0");
        s += c;
        if ((i != hp.size() - 1) && !((i + 1) % 4))
            s += "-";
    }
    bool b = false;
    toHashpass(s, &b);
    if (!b)
        return bRet(ok, false, QString());
    return bRet(ok, true, s);
}

QString userIp(const cppcms::http::request &req, bool *proxy)
{
    SettingsLocker s;
    cppcms::http::request &r = *const_cast<cppcms::http::request *>(&req);
    bSet(proxy, false);
    if (s->value("System/Proxy/detect_real_ip", true).toBool()) {
        QString ip = fromStd(r.getenv("HTTP_X_FORWARDED_FOR"));
        bool ok = false;
        ipNum(ip, &ok);
        if (ok)
            return bRet(proxy, true, ip);
        ip = fromStd(r.getenv("HTTP_X_CLIENT_IP"));
        ipNum(ip, &ok);
        if (ok)
            return bRet(proxy, true, ip);
    }
    if (s->value("System/use_x_real_ip", false).toBool())
        return fromStd(r.getenv("HTTP_X_REAL_IP"));
    else
        return fromStd(r.remote_addr());
}

}
