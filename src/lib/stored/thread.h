#ifndef THREAD_H
#define THREAD_H

class FileInfo;
class Post;
class PostReference;

#include "../global.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QSharedPointer>
#include <QString>
#include <QVariant>

#include <odb/core.hxx>
#include <odb/qt/lazy-ptr.hxx>

PRAGMA_DB(object table("threads"))
class OLOLORD_EXPORT Thread
{
public:
    typedef QList< QLazyWeakPointer<Post> > Posts;
private:
    PRAGMA_DB(id auto)
    quint64 id_;
    PRAGMA_DB(index unique member(id_))
    PRAGMA_DB(not_null)
    QString board_;
    PRAGMA_DB(index member(board_))
    PRAGMA_DB(not_null)
    quint64 number_;
    PRAGMA_DB(index("board_number_index_threads") unique members(board_, number_))
    PRAGMA_DB(index("board_number_archived_index") unique members(board_, number_, archived_))
    PRAGMA_DB(not_null)
    QDateTime dateTime_;
    bool archived_;
    PRAGMA_DB(index("board_archived_index") members(board_, archived_))
    bool fixed_;
    bool postingEnabled_;
    PRAGMA_DB(value_not_null value_type("INTEGER") inverse(thread_))
    Posts posts_;
    bool draft_;
    PRAGMA_DB(not_null)
    QDateTime creationDateTime_;
public:
    explicit Thread(const QString &board, quint64 number, const QDateTime &dateTime);
private:
    explicit Thread();
public:
    quint64 id() const;
    QString board() const;
    quint64 number() const;
    QDateTime dateTime() const;
    QDateTime creationDateTime() const;
    bool archived() const;
    bool fixed() const;
    bool postingEnabled() const;
    const Posts &posts() const;
    Posts &posts();
    bool draft() const;
    void setArchived(bool archived);
    void setBoard(const QString &board);
    void setNumber(quint64 number);
    void setDateTime(const QDateTime &dateTime);
    void setFixed(bool fixed);
    void setPostingEnabled(bool enabled);
    void setDraft(bool draft);
private:
    friend class odb::access;
};

PRAGMA_DB(view object(Thread))
struct OLOLORD_EXPORT ThreadCount
{
    PRAGMA_DB(column("count(" + Thread::id_ + ")"))
    int count;
};

PRAGMA_DB(view object(Thread))
struct OLOLORD_EXPORT ThreadIdDateTimeFixed
{
    quint64 id;
    quint64 number;
    QDateTime dateTime;
    bool fixed;
};

PRAGMA_DB(object table("posts"))
class OLOLORD_EXPORT Post
{
public:
    typedef QList< QLazyWeakPointer<FileInfo> > FileInfos;
    typedef QList< QLazyWeakPointer<PostReference> > PostReferences;
private:
    PRAGMA_DB(id auto)
    quint64 id_;
    PRAGMA_DB(index unique member(id_))
    PRAGMA_DB(not_null)
    QString board_;
    PRAGMA_DB(not_null)
    quint64 number_;
    PRAGMA_DB(index("board_number_index_posts") unique members(board_, number_))
    PRAGMA_DB(not_null)
    QDateTime dateTime_;
    QDateTime modificationDateTime_;
    bool bannedFor_;
    bool showTripcode_;
    QString email_;
    PRAGMA_DB(value_not_null value_type("TEXT") inverse(post_))
    FileInfos fileInfos_;
    QByteArray hashpass_;
    QString name_;
    bool draft_;
    PRAGMA_DB(index("board_draft_index") members(board_, draft_))
    PRAGMA_DB(not_null)
    QByteArray password_;
    QString posterIp_;
    PRAGMA_DB(index("board_posterIp_index") members(board_, posterIp_))
    QString countryCode_;
    QString countryName_;
    QString cityName_;
    bool rawHtml_;
    bool extendedWakabaMarkEnabled_;
    bool bbCodeEnabled_;
    bool signAsOp_;
    QString rawText_;
    PRAGMA_DB(value_not_null value_type("INTEGER") inverse(targetPost_))
    PostReferences referencedBy_;
    PRAGMA_DB(value_not_null value_type("INTEGER") inverse(sourcePost_))
    PostReferences refersTo_;
    QString subject_;
    QString text_;
    QByteArray userData_;
    PRAGMA_DB(not_null)
    QLazySharedPointer<Thread> thread_;
    PRAGMA_DB(index member(thread_))
    PRAGMA_DB(index("board_thread_index") members(board_, thread_))
    PRAGMA_DB(index("board_thread_number_index") members(board_, thread_, number_))
public:
    explicit Post();
    explicit Post(const QString &board, quint64 number, const QDateTime &dateTime, QSharedPointer<Thread> thread,
                  const QString &posterIp, const QString &countryCode, const QString &countryName,
                  const QString &cityName, const QByteArray &password, const QByteArray &hashpass = QByteArray(),
                  bool signAsOp = false);
public:
    quint64 id() const;
    QString board() const;
    quint64 number() const;
    QDateTime dateTime() const;
    QDateTime modificationDateTime() const;
    bool bannedFor() const;
    bool showTripcode() const;
    QString email() const;
    const FileInfos &fileInfos() const;
    FileInfos &fileInfos();
    QByteArray hashpass() const;
    QString name() const;
    QByteArray password() const;
    bool draft() const;
    QString posterIp() const;
    QString countryCode() const;
    QString countryName() const;
    QString cityName() const;
    bool rawHtml() const;
    bool extendedWakabaMarkEnabled() const;
    bool bbCodeEnabled() const;
    bool signAsOp() const;
    QString rawText() const;
    PostReferences referencedBy() const;
    PostReferences refersTo() const;
    void setBoard(const QString &board);
    void setNumber(quint64 number);
    void setDateTime(const QDateTime &dt);
    void setModificationDateTime(const QDateTime &dt);
    void setBannedFor(bool banned);
    void setShowTripcode(bool show);
    void setEmail(const QString &email);
    void setName(const QString &name);
    void setDraft(bool draft);
    void setRawHtml(bool raw);
    void setExtendedWakabaMarkEnabled(bool b);
    void setBbCodeEnabled(bool b);
    void setRawText(const QString &text);
    void setSubject(const QString &subject);
    void setText(const QString &text);
    void setUserData(const QVariant &data);
    QString subject() const;
    QString text() const;
    QLazySharedPointer<Thread> thread() const;
    QVariant userData() const;
private:
    friend class odb::access;
};

PRAGMA_DB(view object(Post))
struct OLOLORD_EXPORT PostCount
{
    PRAGMA_DB(column("count(" + Post::id_ + ")"))
    int count;
};

PRAGMA_DB(view object(Post))
struct OLOLORD_EXPORT PostId
{
    quint64 id;
};

PRAGMA_DB(view object(Post))
struct OLOLORD_EXPORT PostIdBoardRawText
{
    quint64 id;
    QString board;
    QString rawText;
};

PRAGMA_DB(object table("postReferences"))
class OLOLORD_EXPORT PostReference
{
private:
    PRAGMA_DB(id auto)
    quint64 id_;
    PRAGMA_DB(not_null)
    QLazySharedPointer<Post> sourcePost_;
    PRAGMA_DB(index member(sourcePost_))
    PRAGMA_DB(not_null)
    QLazySharedPointer<Post> targetPost_;
    PRAGMA_DB(index member(targetPost_))
    PRAGMA_DB(index("sourcePost_targetPost_index") unique members(sourcePost_, targetPost_))
public:
    explicit PostReference();
    explicit PostReference(QSharedPointer<Post> sourcePost, QSharedPointer<Post> targetPost);
public:
    QLazySharedPointer<Post> sourcePost() const;
    QLazySharedPointer<Post> targetPost() const;
private:
    friend class odb::access;
};

PRAGMA_DB(object table("fileInfos"))
class OLOLORD_EXPORT FileInfo
{
private:
    PRAGMA_DB(id)
    QString name_;
    PRAGMA_DB(index member(name_))
    PRAGMA_DB(not_null)
    QByteArray hash_;
    PRAGMA_DB(index member(hash_))
    PRAGMA_DB(not_null)
    QString mimeType_;
    PRAGMA_DB(not_null)
    int size_;
    PRAGMA_DB(not_null)
    int height_;
    PRAGMA_DB(not_null)
    int width_;
    PRAGMA_DB(not_null)
    QString thumbName_;
    PRAGMA_DB(not_null)
    int thumbHeight_;
    PRAGMA_DB(not_null)
    int thumbWidth_;
    QByteArray metaData_;
    int rating_;
    PRAGMA_DB(not_null)
    QLazySharedPointer<Post> post_;
public:
    explicit FileInfo();
    explicit FileInfo(const QString &name, const QByteArray &hash, const QString &mimeType, int size, int height,
                      int width, const QString &thumbName, int thumbHeight, int thumbWidth, const QVariant &metaData,
                      int rating, QSharedPointer<Post> post);
public:
    QString name() const;
    QByteArray hash() const;
    QString mimeType() const;
    int size() const;
    int height() const;
    int width() const;
    QString thumbName() const;
    int thumbHeight() const;
    int thumbWidth() const;
    QVariant metaData() const;
    int rating() const;
    QLazySharedPointer<Post> post() const;
    void setMetaData(const QVariant &metaData);
private:
    friend class odb::access;
};

PRAGMA_DB(view object(FileInfo))
struct OLOLORD_EXPORT FileInfoCount
{
    PRAGMA_DB(column("count(" + FileInfo::name_ + ")"))
    int count;
};

#endif // THREAD_H
