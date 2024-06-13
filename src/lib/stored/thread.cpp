#include "thread.h"

#include <BeQt>

#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <QList>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <odb/qt/lazy-ptr.hxx>

Thread::Thread(const QString &board, quint64 number, const QDateTime &dateTime)
{
    id_ = 0L;
    board_ = board;
    number_ = number;
    dateTime_ = dateTime.toUTC();
    creationDateTime_ = dateTime_;
    archived_ = false;
    fixed_ = false;
    postingEnabled_ = true;
    draft_ = false;
}

Thread::Thread()
{
    //
}

quint64 Thread::id() const
{
    return id_;
}

QString Thread::board() const
{
    return board_;
}

quint64 Thread::number() const
{
    return number_;
}

QDateTime Thread::dateTime() const
{
    return QDateTime(dateTime_.date(), dateTime_.time(), Qt::UTC);
}

QDateTime Thread::creationDateTime() const
{
    return QDateTime(creationDateTime_.date(), creationDateTime_.time(), Qt::UTC);
}

bool Thread::archived() const
{
    return archived_;
}

bool Thread::fixed() const
{
    return fixed_;
}

bool Thread::postingEnabled() const
{
    return postingEnabled_;
}

const Thread::Posts &Thread::posts() const
{
    return posts_;
}

Thread::Posts &Thread::posts()
{
    return posts_;
}

bool Thread::draft() const
{
    return draft_;
}

void Thread::setArchived(bool archived)
{
    archived_ = archived;
}

void Thread::setBoard(const QString &board)
{
    board_ = board;
}

void Thread::setNumber(quint64 number)
{
    number_ = number;
}

void Thread::setDateTime(const QDateTime &dateTime)
{
    dateTime_ = dateTime;
}

void Thread::setFixed(bool fixed)
{
    fixed_ = fixed;
}

void Thread::setPostingEnabled(bool enabled)
{
    postingEnabled_ = enabled;
}

void Thread::setDraft(bool draft)
{
    draft_ = draft;
}

Post::Post()
{
    //
}

Post::Post(const QString &board, quint64 number, const QDateTime &dateTime, QSharedPointer<Thread> thread,
           const QString &posterIp, const QString &countryCode, const QString &countryName, const QString &cityName,
           const QByteArray &password, const QByteArray &hashpass, bool signAsOp)
{
    id_ = 0L;
    board_ = board;
    number_ = number;
    dateTime_ = dateTime.toUTC();
    modificationDateTime_ = QDateTime().toUTC();
    hashpass_ = hashpass;
    bannedFor_ = false;
    showTripcode_ = false;
    thread_ = thread;
    posterIp_ = posterIp;
    countryCode_ = countryCode;
    countryName_ = countryName;
    cityName_ = cityName;
    rawHtml_ = false;
    draft_ = false;
    extendedWakabaMarkEnabled_ = true;
    bbCodeEnabled_ = true;
    signAsOp_ = signAsOp;
    password_ = password;
}

quint64 Post::id() const
{
    return id_;
}

QString Post::board() const
{
    return board_;
}

quint64 Post::number() const
{
    return number_;
}

QDateTime Post::dateTime() const
{
    return QDateTime(dateTime_.date(), dateTime_.time(), Qt::UTC);
}

QDateTime Post::modificationDateTime() const
{
    return QDateTime(modificationDateTime_.date(), modificationDateTime_.time(), Qt::UTC);
}

bool Post::bannedFor() const
{
    return bannedFor_;
}

bool Post::showTripcode() const
{
    return showTripcode_;
}

QString Post::email() const
{
    return email_;
}

const Post::FileInfos &Post::fileInfos() const
{
    return fileInfos_;
}

Post::FileInfos &Post::fileInfos()
{
    return fileInfos_;
}

QByteArray Post::hashpass() const
{
    return hashpass_;
}

QString Post::name() const
{
    return name_;
}

QByteArray Post::password() const
{
    return password_;
}

bool Post::draft() const
{
    return draft_;
}

QString Post::posterIp() const
{
    return posterIp_;
}

QString Post::countryCode() const
{
    return countryCode_;
}

QString Post::countryName() const
{
    return countryName_;
}

QString Post::cityName() const
{
    return cityName_;
}

bool Post::rawHtml() const
{
    return rawHtml_;
}

bool Post::extendedWakabaMarkEnabled() const
{
    return extendedWakabaMarkEnabled_;
}

bool Post::bbCodeEnabled() const
{
    return bbCodeEnabled_;
}

bool Post::signAsOp() const
{
    return signAsOp_;
}

QString Post::rawText() const
{
    return rawText_;
}

Post::PostReferences Post::referencedBy() const
{
    return referencedBy_;
}

Post::PostReferences Post::refersTo() const
{
    return refersTo_;
}

void Post::setBoard(const QString &board)
{
    board_ = board;
}

void Post::setNumber(quint64 number)
{
    number_ = number;
}

void Post::setDateTime(const QDateTime &dt)
{
    dateTime_ = dt.toUTC();
}

void Post::setModificationDateTime(const QDateTime &dt)
{
    modificationDateTime_ = dt.toUTC();
}

void Post::setBannedFor(bool banned)
{
    bannedFor_ = banned;
}

void Post::setShowTripcode(bool show)
{
    showTripcode_ = show;
}

void Post::setEmail(const QString &email)
{
    email_ = email;
}

void Post::setName(const QString &name)
{
    name_ = name;
}

void Post::setDraft(bool draft)
{
    draft_ = draft;
}

void Post::setRawHtml(bool raw)
{
    rawHtml_ = raw;
}

void Post::setExtendedWakabaMarkEnabled(bool b)
{
    extendedWakabaMarkEnabled_ = b;
}

void Post::setBbCodeEnabled(bool b)
{
    bbCodeEnabled_ = b;
}

void Post::setRawText(const QString &text)
{
    rawText_ = text;
}

void Post::setSubject(const QString &subject)
{
    subject_ = subject;
}

void Post::setText(const QString &text)
{
    text_ = text;
}

void Post::setUserData(const QVariant &data)
{
    userData_ = BeQt::serialize(data);
}

QString Post::subject() const
{
    return subject_;
}

QString Post::text() const
{
    return text_;
}

QLazySharedPointer<Thread> Post::thread() const
{
    return thread_;
}

QVariant Post::userData() const
{
    return BeQt::deserialize(userData_);
}

PostReference::PostReference()
{
    //
}

PostReference::PostReference(QSharedPointer<Post> sourcePost, QSharedPointer<Post> targetPost)
{
    id_ = 0L;
    sourcePost_ = sourcePost;
    targetPost_ = targetPost;
}

QLazySharedPointer<Post> PostReference::sourcePost() const
{
    return sourcePost_;
}

QLazySharedPointer<Post> PostReference::targetPost() const
{
    return targetPost_;
}

FileInfo::FileInfo()
{
    //
}

FileInfo::FileInfo(const QString &name, const QByteArray &hash, const QString &mimeType, int size, int height,
                   int width, const QString &thumbName, int thumbHeight, int thumbWidth, const QVariant &metaData,
                   int rating, QSharedPointer<Post> post)
{
    name_ = name;
    hash_ = hash;
    mimeType_ = mimeType;
    size_ = size;
    height_ = height;
    width_ = width;
    thumbName_ = thumbName;
    thumbHeight_ = thumbHeight;
    thumbWidth_ = thumbWidth;
    metaData_ = BeQt::serialize(metaData);
    rating_ = rating;
    post_ = post;
}

QString FileInfo::name() const
{
    return name_;
}

QByteArray FileInfo::hash() const
{
    return hash_;
}

QString FileInfo::mimeType() const
{
    return mimeType_;
}

int FileInfo::size() const
{
    return size_;
}

int FileInfo::height() const
{
    return height_;
}

int FileInfo::width() const
{
    return width_;
}

QString FileInfo::thumbName() const
{
    return thumbName_;
}

int FileInfo::thumbHeight() const
{
    return thumbHeight_;
}

int FileInfo::thumbWidth() const
{
    return thumbWidth_;
}

QVariant FileInfo::metaData() const
{
    return BeQt::deserialize(metaData_);
}

int FileInfo::rating() const
{
    return rating_;
}

QLazySharedPointer<Post> FileInfo::post() const
{
    return post_;
}

void FileInfo::setMetaData(const QVariant &metaData)
{
    metaData_ = BeQt::serialize(metaData);
}
