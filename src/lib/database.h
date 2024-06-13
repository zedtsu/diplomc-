#ifndef DATABASE_H
#define DATABASE_H

namespace Content
{

class Post;

}

class Post;

namespace Search
{

class Query;

}

namespace Tools
{

class AudioTags;
class File;
class Post;

}

class QLocale;

namespace cppcms
{

namespace http
{

class request;

}

}

#include "global.h"
#include "stored/registereduser.h"
#include "stored/thread.h"
#include "transaction.h"

#include <BCoreApplication>

#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <QList>
#include <QMap>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <cppcms/json.h>

#ifndef OLOLORD_NO_ODB
#include <odb/database.hxx>
#include <odb/query.hxx>
#endif

namespace Database
{

#ifndef OLOLORD_NO_ODB
template <typename ResultType> class Result
{
public:
    const bool error;
public:
    QSharedPointer<ResultType> data;
public:
    explicit Result(bool err = true) :
        error(err)
    {
        //
    }
    explicit Result(const odb::result_iterator<ResultType> &i) :
        error(false)
    {
        data = QSharedPointer<ResultType>(new ResultType(*i));
    }
    Result(const Result<ResultType> &other) :
        error(other.error)
    {
        *this = other;
    }
public:
    void clear()
    {
        data.clear();
    }
public:
    ResultType *operator ->() const
    {
        return data.data();
    }
    ResultType &operator *() const
    {
        return *data;
    }
    bool operator !() const
    {
        return data.isNull();
    }
    Result<ResultType> &operator =(const Result<ResultType> &other)
    {
        data = other.data;
        *const_cast<bool *>(&error) = other.error;
        return *this;
    }
    Result<ResultType> &operator =(ResultType *t)
    {
        data = QSharedPointer<ResultType>(t);
        return *this;
    }
    operator bool() const
    {
        return !data.isNull();
    }
};

template <typename ResultType, typename QueryType> QList<ResultType> query(const odb::query<QueryType> &q)
{
    odb::result<ResultType> r(Transaction()->query<ResultType>(q));
    QList<ResultType> list;
    for (odb::result_iterator<ResultType> i = r.begin(); i != r.end(); ++i)
        list << *i;
    return list;
}

template <typename ResultType, typename QueryType> QList<ResultType> query(const QString &q)
{
    odb::result<ResultType> r(Transaction()->query<ResultType>(q.toUtf8().constData()));
    QList<ResultType> list;
    for (odb::result_iterator<ResultType> i = r.begin(); i != r.end(); ++i)
        list << *i;
    return list;
}

template <typename ResultType> QList<ResultType> queryAll()
{
    odb::result<ResultType> r(Transaction()->query<ResultType>());
    QList<ResultType> list;
    for (odb::result_iterator<ResultType> i = r.begin(); i != r.end(); ++i)
        list << *i;
    return list;
}

template <typename ResultType, typename QueryType> Result<ResultType> queryOne(const odb::query<QueryType> &q)
{
    odb::result<ResultType> r(Transaction()->query<ResultType>(q));
    odb::result_iterator<ResultType> i = r.begin();
    if (r.end() == i)
        return Result<ResultType>(false);
    Result<ResultType> v(i);
    ++i;
    if (r.end() != i)
        return Result<ResultType>(true);
    return v;
}

template <typename T> bool persist(const Result<T> &t)
{
    if (!t)
        return false;
    Transaction(true)->persist(*t);
    return true;
}

template <typename T> bool update(const Result<T> &t)
{
    if (!t)
        return false;
    Transaction(true)->update(*t);
    return true;
}

template <typename T> bool erase(const Result<T> &t)
{
    if (!t)
        return false;
    Transaction(true)->erase(*t);
    return true;
}
#endif

struct OLOLORD_EXPORT BanInfo
{
    QString boardName;
    QDateTime dateTime;
    QDateTime expires;
    int level;
    QString reason;
public:
    bool isExpired() const;
};

struct OLOLORD_EXPORT GeolocationInfo
{
    QString cityName;
    QString countryCode;
    QString countryName;
    QString ip;
};

struct OLOLORD_EXPORT RefKey
{
    QString boardName;
    quint64 postNumber;
public:
    explicit RefKey();
    explicit RefKey(const QString &board, quint64 post);
public:
    bool isValid() const;
public:
    bool operator <(const RefKey &other) const;
};

class OLOLORD_EXPORT RefMap : public QMap<RefKey, quint64>
{
    //
};

struct OLOLORD_EXPORT CreatePostParameters
{
    const QList<Tools::File> &files;
    const QLocale &locale;
    const QMap<QString, QString> &params;
    const cppcms::http::request &request;
public:
    unsigned int bumpLimit;
    unsigned int postLimit;
    QString *error;
    QString *description;
    RefMap referencedPosts;
public:
    explicit CreatePostParameters(const cppcms::http::request &req, const QMap<QString, QString> &ps,
                                  const QList<Tools::File> &fs, const QLocale &l = BCoreApplication::locale());
};

struct OLOLORD_EXPORT CreateThreadParameters
{
    const QList<Tools::File> &files;
    const QLocale &locale;
    const QMap<QString, QString> &params;
    const cppcms::http::request &request;
public:
    unsigned int archiveLimit;
    unsigned int threadLimit;
    QString *error;
    QString *description;
public:
    explicit CreateThreadParameters(const cppcms::http::request &req, const QMap<QString, QString> &ps,
                                    const QList<Tools::File> &fs, const QLocale &l = BCoreApplication::locale());
};

struct OLOLORD_EXPORT EditPostParameters
{
    const QString &boardName;
    const quint64 postNumber;
    const cppcms::http::request &request;
public:
    bool draft;
    QString email;
    QString *error;
    QString name;
    QByteArray password;
    bool raw;
    QString subject;
    QString text;
    bool extendedWakabaMarkEnabled;
    bool bbCodeEnabled;
    cppcms::json::value userData;
    RefMap referencedPosts;
public:
    explicit EditPostParameters(const cppcms::http::request &req, const QString &board, quint64 post);
};

OLOLORD_EXPORT bool addFile(const cppcms::http::request &req, const QMap<QString, QString> &params,
                            const QList<Tools::File> &files, QString *error = 0, QString *description = 0);
OLOLORD_EXPORT int addPostsToIndex(QString *error = 0, const QLocale &l = BCoreApplication::locale());
OLOLORD_EXPORT QMap< QString, QMap<QString, BanInfo> > banInfos(bool *ok = 0, QString *error = 0,
                                                                const QLocale &l = BCoreApplication::locale());
OLOLORD_EXPORT bool banUser(const QString &ip, const QList<BanInfo> &bans, QString *error = 0,
                            const QLocale &l = BCoreApplication::locale());
OLOLORD_EXPORT bool banUser(const QString &sourceBoard, quint64 postNumber, const QList<BanInfo> &bans,
                            QString *error = 0, const QLocale &l = BCoreApplication::locale());
OLOLORD_EXPORT bool banUser(const cppcms::http::request &req, const QString &ip, const QList<BanInfo> &bans,
                            QString *error = 0);
OLOLORD_EXPORT bool banPoster(const cppcms::http::request &req, const QString &sourceBoard, quint64 postNumber,
                              const QList<BanInfo> &bans, QString *error = 0);
OLOLORD_EXPORT void checkOutdatedEntries();
OLOLORD_EXPORT bool createPost(CreatePostParameters &p, quint64 *postNumber = 0);
OLOLORD_EXPORT void createSchema();
OLOLORD_EXPORT quint64 createThread(CreateThreadParameters &p);
OLOLORD_EXPORT bool delall(const cppcms::http::request &req, const QString &ip, const QString &boardName = "*",
                           QString *error = 0);
OLOLORD_EXPORT bool deleteFile(const QString &boardName, const QString &fileName, const cppcms::http::request &req,
                               const QByteArray &password, QString *error = 0);
OLOLORD_EXPORT bool deletePost(const QString &boardName, quint64 postNumber, QString *error = 0,
                               const QLocale &l = BCoreApplication::locale());
OLOLORD_EXPORT bool deletePost(const QString &boardName, quint64 postNumber, const cppcms::http::request &req,
                               const QByteArray &password, QString *error = 0);
OLOLORD_EXPORT bool editAudioTags(const QString &boardName, const QString &fileName, const cppcms::http::request &req,
                                  const QByteArray &password, const QVariantMap &tags, QString *error = 0);
OLOLORD_EXPORT bool editPost(EditPostParameters &p);
OLOLORD_EXPORT unsigned int fileCount(const QString &boardName, quint64 postNumber);
OLOLORD_EXPORT bool fileExists(const QByteArray &hash, bool *ok = 0);
OLOLORD_EXPORT bool fileExists(const QString &hashString, bool *ok = 0);
OLOLORD_EXPORT QList<Post> findPosts(const Search::Query &query, const QString &boardName = QString(), bool *ok = 0,
                                     QString *error = 0, QString *description = 0,
                                     const QLocale &l = BCoreApplication::locale());
OLOLORD_EXPORT void generateRss();
OLOLORD_EXPORT GeolocationInfo geolocationInfo(const QString &ip);
OLOLORD_EXPORT GeolocationInfo geolocationInfo(const cppcms::http::request &req);
OLOLORD_EXPORT GeolocationInfo geolocationInfo(const QString &boardName, quint64 postNumber);
OLOLORD_EXPORT QVariant getFileMetaData(const QString &fileName, bool *ok = 0, QString *error = 0,
                                        const QLocale &l = BCoreApplication::locale());
OLOLORD_EXPORT int getNewPostCount(const cppcms::http::request &req, const QString &boardName, quint64 lastPostNumber,
                                   bool *ok = 0, QString *error = 0);
OLOLORD_EXPORT QVariantMap getNewPostCountEx(const cppcms::http::request &req, const QVariantMap &numbers,
                                             bool *ok = 0, QString *error = 0);
OLOLORD_EXPORT QList<Post> getNewPosts(const cppcms::http::request &req, const QString &boardName,
                                       quint64 threadNumber, quint64 lastPostNumber, bool *ok = 0, QString *error = 0);
OLOLORD_EXPORT QList<Content::Post> getNewPostsC(const cppcms::http::request &req, const QString &boardName,
    quint64 threadNumber, quint64 lastPostNumber, bool *ok = 0, QString *error = 0);
OLOLORD_EXPORT Post getPost(const cppcms::http::request &req, const QString &boardName, quint64 postNumber,
                            bool *ok = 0, QString *error = 0);
OLOLORD_EXPORT Content::Post getPostC(const cppcms::http::request &req, const QString &boardName, quint64 postNumber,
                                     bool *ok = 0, QString *error = 0);
OLOLORD_EXPORT QList<quint64> getThreadNumbers(const cppcms::http::request &req, const QString &boardName,
                                               bool *ok = 0, QString *error = 0);
OLOLORD_EXPORT bool isOp(const QString &boardName, quint64 threadNumber, const QString &userIp,
                         const QByteArray &hashpass = QByteArray());
OLOLORD_EXPORT quint64 lastPostNumber(const QString &boardName, QString *error = 0,
                                      const QLocale &l = BCoreApplication::locale());
OLOLORD_EXPORT bool moderOnBoard(const cppcms::http::request &req, const QString &board1,
                                 const QString &board2 = QString());
OLOLORD_EXPORT bool moderOnBoard(const QByteArray &hashpass, const QString &boardName,
                                 const QString &board2 = QString());
OLOLORD_EXPORT quint64 moveThread(const cppcms::http::request &req, const QString &sourceBoard, quint64 threadNumber,
                                  const QString &targetBoard, QString *error = 0);
OLOLORD_EXPORT bool postExists(const QString &boardName, quint64 postNumber, quint64 *threadNumber = 0);
OLOLORD_EXPORT QString posterIp(const QString &boardName, quint64 postNumber);
OLOLORD_EXPORT quint64 postThreadNumber(const QString &boardName, quint64 postNumber);
OLOLORD_EXPORT QStringList registeredUserBoards(const cppcms::http::request &req);
OLOLORD_EXPORT QStringList registeredUserBoards(const QByteArray &hashpass);
OLOLORD_EXPORT int registeredUserLevel(const cppcms::http::request &req);
OLOLORD_EXPORT int registeredUserLevel(const QByteArray &hashpass);
OLOLORD_EXPORT bool registerUser(const QByteArray &hashpass, RegisteredUser::Level level = RegisteredUser::UserLevel,
                                 const QStringList &boards = QStringList("*"), QString *error = 0,
                                 const QLocale &l = BCoreApplication::locale());
OLOLORD_EXPORT int rerenderPosts(const QStringList boardNames = QStringList(), QString *error = 0,
                                 const QLocale &l = BCoreApplication::locale());
OLOLORD_EXPORT QString rss(const QString &boardName);
OLOLORD_EXPORT bool setThreadFixed(const QString &boardName, quint64 threadNumber, bool fixed, QString *error = 0,
                                   const QLocale &l = BCoreApplication::locale());
OLOLORD_EXPORT bool setThreadFixed(const QString &boardName, quint64 threadNumber, bool fixed,
                                   const cppcms::http::request &req, QString *error = 0);
OLOLORD_EXPORT bool setThreadOpened(const QString &boardName, quint64 threadNumber, bool opened, QString *error = 0,
                                    const QLocale &l = BCoreApplication::locale());
OLOLORD_EXPORT bool setThreadOpened(const QString &boardName, quint64 threadNumber, bool opened,
                                    const cppcms::http::request &req, QString *error = 0);
OLOLORD_EXPORT bool setVoteOpened(quint64 postNumber, bool opened, const QByteArray &password,
                                  const cppcms::http::request &req, QString *error = 0);
OLOLORD_EXPORT bool unvote(quint64 postNumber, const cppcms::http::request &req, QString *error = 0);
OLOLORD_EXPORT QMap<QString, BanInfo> userBanInfo(const QString &ip, bool *ok = 0, QString *error = 0,
                                                  const QLocale &l = BCoreApplication::locale());
OLOLORD_EXPORT QMap<QString, BanInfo> userBanInfo(const QString &boardName, quint64 postNumber, bool *ok = 0,
                                                  QString *error = 0, const QLocale &l = BCoreApplication::locale());
OLOLORD_EXPORT bool vote(quint64 postNumber, const QStringList &votes, const cppcms::http::request &req,
                         QString *error = 0);

}

#endif // DATABASE_H
