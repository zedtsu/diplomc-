#include "controller.h"

#include "board/abstractboard.h"
#include "captcha/abstractcaptchaengine.h"
#include "controller/ban.h"
#include "controller/base.h"
#include "controller/baseboard.h"
#include "controller/error.h"
#include "controller/ipban.h"
#include "controller/notfound.h"
#include "database.h"
#include "error.h"
#include "settingslocker.h"
#include "tools.h"
#include "translator.h"

#include <BCoreApplication>
#include <BeQt>
#include <BTranslation>

#include <QChar>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QLocale>
#include <QMap>
#include <QMutex>
#include <QSettings>
#include <QString>
#include <QStringList>

#include <cppcms/application.h>
#include <cppcms/http_cookie.h>
#include <cppcms/http_request.h>
#include <cppcms/http_response.h>
#include <cppcms/json.h>

#include <list>
#include <string>

namespace Controller
{

static QMutex localeMutex(QMutex::Recursive);

static std::string speedString(const AbstractBoard::PostingSpeed &s, double duptime)
{
    double d = double(s.postCount) / duptime;
    QString ss = QString::number(d, 'f', 1);
    return Tools::toStd((ss.split('.').last() != "0") ? ss : ss.split('.').first());
}

static Content::Base::Locale toWithLocale(const QLocale &l)
{
    Content::Base::Locale ll;
    ll.country = Tools::toStd(l.nativeCountryName());
    ll.name = Tools::toStd(l.name());
    ll.language = Tools::toStd(l.nativeLanguageName());
    return ll;
}

static std::string zeroSpeedString(const AbstractBoard::PostingSpeed &s, const std::string &nonZero, const QLocale &l)
{
    if (s.postCount && s.uptimeMsecs)
        return "1 " + nonZero;
    else
        return "0 " + TranslatorStd(l).translate("zeroSpeedString", "post(s) per hour.", "postingSpeed");
}

void initBase(Content::Base &c, const cppcms::http::request &req, const QString &pageTitle)
{
    typedef std::list<Content::Base::Locale> LocaleList;
    localeMutex.lock();
    init_once(LocaleList, locales, LocaleList()) {
        foreach (const QString &path, BCoreApplication::locations(BCoreApplication::TranslationsPath)) {
            foreach (const QString &fn, QDir(path).entryList(QStringList() << "ololord_*.qm", QDir::Files)) {
                QString ln = fn.mid(8);
                ln.remove(ln.length() - 3, 3);
                QLocale ll(ln);
                if (QLocale::c() == ll)
                    continue;
                Content::Base::Locale lll = toWithLocale(ll);
                if (std::find(locales.begin(), locales.end(), lll) != locales.end())
                    continue;
                locales.push_back(lll);
            }
        }
        locales.push_back(toWithLocale(QLocale("en_US")));
    }
    typedef QMap<QString, BTranslation> TranslationMap;
    init_once(TranslationMap, styles, TranslationMap()) {
        styles.insert("photon", BTranslation::translate("initBase", "Photon", "style title"));
        styles.insert("neutron", BTranslation::translate("initBase", "Neutron", "style title"));
        styles.insert("futaba", BTranslation::translate("initBase", "Futaba", "style title"));
        styles.insert("burichan", BTranslation::translate("initBase", "Burichan", "style title"));
    }
    init_once(TranslationMap, modes, TranslationMap()) {
        modes.insert("normal", BTranslation::translate("initBase", "Normal", "mode title"));
        modes.insert("ascetic", BTranslation::translate("initBase", "Ascetic", "mode title"));
    }
    localeMutex.unlock();
    TranslatorStd ts(req);
    c.addToFavoritesOnReplyLabelText = ts.translate("initBase", "Add thread to favorites on reply:",
                                                    "addToFavoritesOnReplyLabelText");
    c.allBoardsText = ts.translate("initBase", "All boards", "allBoardsText");
    c.autoUpdateIntervalLabelText = ts.translate("initBase", "Auto update interval (sec):",
                                                 "autoUpdateIntervalLabelText");
    c.autoUpdateThreadsByDefaultLabelText = ts.translate("initBase", "Auto update threads by default:",
                                                         "autoUpdateThreadsByDefaultLabelText");
    QStringList userBoards = Database::registeredUserBoards(req);
    if (userBoards.size() == 1 && userBoards.first() == "*") {
        userBoards.clear();
        userBoards << AbstractBoard::boardNames();
    }
    foreach (int i, bRangeD(0, userBoards.size() - 1))
        userBoards[i] += "|" + AbstractBoard::board(userBoards.at(i))->title(ts.locale());
    c.availableBoardsString = Tools::toStd(userBoards.join(";"));
    c.boards = AbstractBoard::boardInfos(ts.locale(), false);
    c.captchaLabelText = ts.translate("initBase", "Captcha:", "captchaLabelText");
    c.captchaLabelWarningText = ts.translate("initBase", "This option may be ignored on some boards",
                                             "captchaLabelWarningText");
    AbstractCaptchaEngine::LockingWrapper ce = AbstractCaptchaEngine::engine(Tools::cookieValue(req, "captchaEngine"));
    if (!ce.isNull()) {
        c.currentCaptchaEngine.id = Tools::toStd(ce->id());
        c.currentCaptchaEngine.title = Tools::toStd(ce->title(ts.locale()));
    }
    AbstractCaptchaEngine::EngineInfoList eilist = AbstractCaptchaEngine::engineInfos(ts.locale());
    foreach (const AbstractCaptchaEngine::EngineInfo &inf, eilist) {
        Content::BaseBoard::CaptchaEngine e;
        e.id = inf.id;
        e.title = inf.title;
        c.captchaEngines.push_back(e);
        if (ce.isNull() && inf.id == "google-recaptcha") {
            c.currentCaptchaEngine.id = inf.id;
            c.currentCaptchaEngine.title = inf.title;
        }
    }
    if (ce.isNull() && c.currentCaptchaEngine.id.empty() && !eilist.isEmpty()) {
        c.currentCaptchaEngine.id = eilist.first().id;
        c.currentCaptchaEngine.title = eilist.first().title;
    }
    c.cancelButtonText = ts.translate("initBase", "Cancel", "cancelButtonText");
    c.checkFileExistenceLabelText = ts.translate("initBase", "Check if attached file exists on server:",
                                                 "checkFileExistenceLabelText");
    c.closeButtonText = ts.translate("initBase", "Close", "closeButtonText");
    c.confirmButtonText = ts.translate("initBase", "Confirm", "confirmButtonText");
    c.currentLocale = toWithLocale(ts.locale());
    cppcms::http::request *mreq = const_cast<cppcms::http::request *>(&req);
    c.currentTime = mreq->cookie_by_name("time").value();
    QString deviceType = Tools::isMobile(req).any ? "mobile" : "desktop";
    c.customFooterContent = Tools::toStd(Tools::customContent("footer", ts.locale()).replace("%deviceType%",
                                                                                             deviceType));
    c.customHeaderContent = Tools::toStd(Tools::customContent("header", ts.locale()).replace("%deviceType%",
                                                                                             deviceType));
    foreach (const Tools::CustomLinkInfo &info, Tools::customLinks(ts.locale())) {
        Content::Base::CustomLinkInfo inf;
        inf.imgUrl = Tools::toStd(info.imgUrl);
        inf.target = Tools::toStd(info.target);
        inf.text = Tools::toStd(info.text);
        inf.url = Tools::toStd(info.url);
        c.customLinks.push_back(inf);
    }
    c.defaultAudioVideoVolumeLabelText = ts.translate("initBase", "Default audio and video files volume:",
                                                      "defaultAudioVideoVolumeLabelText");
    c.deviceType = Tools::toStd(deviceType);
    c.draftsByDefault = !Tools::cookieValue(req, "draftsByDefault").compare("true", Qt::CaseInsensitive);
    c.draftsByDefaultLabelText = ts.translate("initBase", "Mark posts as drafts by default:",
                                              "draftsByDefaultLabelText");
    c.editHotkeysText = ts.translate("initBase", "Edit", "editHotkeysText");
    c.editSpellsText = ts.translate("initBase", "Edit", "editSpellsText");
    c.editUserCssText = ts.translate("initBase", "Edit", "editUserCssText");
    c.error413Text = ts.translate("initBase", "Request entity too large", "error413Text");
    c.favoriteThreadsText = ts.translate("initBase", "Favorite threads", "favoriteThreadsText");
    c.filesTabText = ts.translate("initBase", "Files", "filesTabText");
    c.framedVersionText = ts.translate("initBase", "Framed version", "framedVersionText");
    c.generalSettingsLegendText = ts.translate("initBase", "General settings", "generalSettingsLegendText");
    foreach (const QString &bn, Tools::cookieValue(req, "hiddenBoards").split('|', QString::SkipEmptyParts))
        c.hiddenBoards.insert(Tools::toStd(bn));
    c.hiddenBoardsLabelText = ts.translate("initBase", "Hide boards:", "hiddenBoardsLabelText");
    c.hiddenPostListText = ts.translate("initBase", "Hidden posts/threads", "hiddenPostListText");
    c.hidePostformMarkupLabelText = ts.translate("initBase", "Hide postform markup:", "hidePostformMarkupLabelText");
    c.hidePostformRules = !Tools::cookieValue(req, "hidePostformRules").compare("true", Qt::CaseInsensitive);
    c.hidePostformRulesLabelText = ts.translate("initBase", "Hide postform rules:", "hidePostformRulesLabelText");
    c.hideTripcodesLabelText = ts.translate("initBase", "Hide tripcodes:", "hideTripcodesLabelText");
    c.hideUserNamesLabelText = ts.translate("initBase", "Hide user names:", "hideUserNamesLabelText");
    c.hidingTabText = ts.translate("initBase", "Hiding", "hidingTabText");
    c.hotkeyExpandImageLabelText = ts.translate("initBase", "Expand post file", "hotkeyExpandImageLabelText");
    c.hotkeyExpandThreadLabelText = ts.translate("initBase", "Expand thread", "hotkeyExpandThreadLabelText");
    c.hotkeyGoToThreadLabelText = ts.translate("initBase", "Go to thread", "hotkeyGoToThreadLabelText");
    c.hotkeyHidePostLabelText = ts.translate("initBase", "Hide post/thread", "hotkeyHidePostLabelText");
    c.hotkeyMarkupBoldLabelText = ts.translate("initBase", "Bold text", "hotkeyMarkupBoldLabelText");
    c.hotkeyMarkupCodeLabelText = ts.translate("initBase", "Code block", "hotkeyMarkupCodeLabelText");
    c.hotkeyMarkupItalicsLabelText = ts.translate("initBase", "Italics", "hotkeyMarkupItalicsLabelText");
    c.hotkeyMarkupQutationLabelText = ts.translate("initBase", "Quote selected text", "hotkeyMarkupQutationLabelText");
    c.hotkeyMarkupSpoilerLabelText = ts.translate("initBase", "Spoiler", "hotkeyMarkupSpoilerLabelText");
    c.hotkeyMarkupStrikedOutLabelText = ts.translate("initBase", "Striked out text",
                                                     "hotkeyMarkupStrikedOutLabelText");
    c.hotkeyMarkupUnderlinedLabelText = ts.translate("initBase", "Underlined text", "hotkeyMarkupUnderlinedLabelText");
    c.hotkeyNextPageImageLabelText = ts.translate("initBase", "Next page/file", "hotkeyNextPageImageLabelText");
    c.hotkeyNextPostLabelText = ts.translate("initBase", "Next post (inthread/on board)", "hotkeyNextPostLabelText");
    c.hotkeyNextThreadPostLabelText = ts.translate("initBase", "Next thread (on board)/post (in thread)",
                                                   "hotkeyNextThreadPostLabelText");
    c.hotkeyPreviousPageImageLabelText = ts.translate("initBase", "Previous page/file",
                                                      "hotkeyPreviousPageImageLabelText");
    c.hotkeyPreviousPostLabelText = ts.translate("initBase", "Previous post (in thread/on board)",
                                                 "hotkeyPreviousPostLabelText");
    c.hotkeyPreviousThreadPostLabelText = ts.translate("initBase", "Previous thread (on board)/post (in thread)",
                                                       "hotkeyPreviousThreadPostLabelText");
    c.hotkeyQuickReplyLabelText = ts.translate("initBase", "Quick reply", "hotkeyQuickReplyLabelText");
    c.hotkeyShowFavoritesLabelText = ts.translate("initBase", "Show favorite threads", "hotkeyShowFavoritesLabelText");
    c.hotkeyShowSettingsLabelText = ts.translate("initBase", "Show settings", "hotkeyShowSettingsLabelText");
    c.hotkeysLabelText = ts.translate("initBase", "Hotkeys:", "hotkeysLabelText");
    c.hotkeySubmitReplyLabelText = ts.translate("initBase", "Submit reply", "hotkeySubmitReplyLabelText");
    c.hotkeyUpdateThreadLabelText = ts.translate("initBase", "Update thread (in thread only)",
                                                 "hotkeyUpdateThreadLabelText");
    c.imageZoomSensitivityLabelText = ts.translate("initBase", "Image zoom sensitivity, %:",
                                                   "imageZoomSensitivityLabelText");
    c.leafThroughImagesOnlyLabelText = ts.translate("initBase", "Leaf through images only:",
                                                    "leafThroughImagesOnlyLabelText");
    c.localeLabelText = "Language:";
    c.locales = locales;
    c.loggedIn = !Tools::hashpassString(req).isEmpty();
    c.loginButtonText = c.loggedIn ? ts.translate("initBase", "Logout", "loginButtonText")
                                   : ts.translate("initBase", "Login", "loginButtonText");
    c.loginLabelText = ts.translate("initBase", "Login:", "loginLabelText");
    if (c.loggedIn) {
        int lvl = Database::registeredUserLevel(req);
        if (lvl < 0) {
            c.loginIconName = "user.png";
            c.loginMessageText = ts.translate("initBase", "Logged in, but not registered", "loginMessageText");
        } else {
            c.loginMessageText = ts.translate("initBase", "Registered and logged in", "loginMessageText");
            if (lvl >= RegisteredUser::AdminLevel) {
                c.loginIconName = "admin.png";
                c.loginMessageText += " (" + ts.translate("initBase", "admin", "loginMessageText") + ")";
            } else if (lvl >= RegisteredUser::ModerLevel) {
                c.loginIconName = "moder.png";
                c.loginMessageText += " (" + ts.translate("initBase", "moder", "loginMessageText") + ")";
            } else if (lvl >= RegisteredUser::UserLevel) {
                c.loginIconName = "user_registered.png";
                c.loginMessageText += " (" + ts.translate("initBase", "user", "loginMessageText") + ")";
            }
        }
    }
    c.loginPlaceholderText = ts.translate("initBase", "Password/hashpass", "loginPlaceholderText");
    c.loginSystemDescriptionText = ts.translate("initBase", "\"Login\", you say? On an imageboard? I am out!\n\n"
                                                "Please, wait a sec. The login systyem does NOT store any data on the "
                                                "server. It only stores a cookie on your PC to allow post editing, "
                                                "deleting, etc. without entering password every time, and nothing "
                                                "else.\n\n"
                                                "Well, actually, the admin may register someone manually (if he is a "
                                                "fag), but there is no way to register through the web.",
                                                "loginSystemDescriptionText");
    c.loopAudioVideoLabelText = ts.translate("initBase", "Loop audio and video files:", "loopAudioVideoLabelText");
    c.maxAllowedRating = 180;
    QString r = Tools::cookieValue(req, "maxAllowedRating");
    if (!r.compare("SFW", Qt::CaseInsensitive))
        c.maxAllowedRating = 0;
    if (!r.compare("R-15", Qt::CaseInsensitive))
        c.maxAllowedRating = 15;
    else if (!r.compare("R-18", Qt::CaseInsensitive))
        c.maxAllowedRating = 18;
    c.maxAllowedRatingLabelText = ts.translate("initBase", "Maximum allowed rating:", "maxAllowedRatingLabelText");
    c.maxSimultaneousAjaxLabelText = ts.translate("initBase", "Maximum simultaneous AJAX requests:",
                                                  "maxSimultaneousAjaxLabelText");
    c.maxSearchQueryLength = 150;
    c.minimalisticPostform = !Tools::cookieValue(req, "minimalisticPostform").compare("true", Qt::CaseInsensitive);
    c.minimalisticPostformLabelText = ts.translate("initBase", "Use minimalistic post form:",
                                                   "minimalisticPostformLabelText");
    c.mode.name = Tools::toStd(Tools::cookieValue(req, "mode"));
    if (c.mode.name.empty())
        c.mode.name = "normal";
    BTranslation t = modes.value(Tools::fromStd(c.mode.name));
    c.mode.title = ts.translate(t.context().toUtf8().constData(), t.sourceText().toUtf8().constData(),
                                t.disambiguation().toUtf8().constData());
    c.modeLabelText = ts.translate("initBase", "Mode:", "modeLabelText");
    foreach (const QString &s, modes.keys()) {
        Content::Base::Mode m;
        m.name = Tools::toStd(s);
        t = modes.value(s);
        m.title = ts.translate(t.context().toUtf8().constData(), t.sourceText().toUtf8().constData(),
                               t.disambiguation().toUtf8().constData());
        c.modes.push_back(m);
    }
    c.moder = Database::registeredUserLevel(req) / 10;
    c.moveToPostOnReplyInThreadLabelText = ts.translate("initBase", "Move to post after replying in thread:",
                                                        "moveToPostOnReplyInThreadLabelText");
    c.mumWatchingText = ts.translate("initBase", "Mum is watching me!", "mumWatchingText");
    c.otherTabText = ts.translate("initBase", "Other", "otherTabText");
    c.pageTitle = Tools::toStd(pageTitle);
    c.path = const_cast<cppcms::http::request *>(&req)->path_info();
    c.playAudioVideoImmediatelyLabelText = ts.translate("initBase", "Play audio and video files immediately:",
                                                        "playAudioVideoImmediatelyLabelText");
    c.postformTabText = ts.translate("initBase", "Postform and posting", "postformTabText");
    c.postShrinkingLabelText = ts.translate("initBase", "Shrink posts:", "postShrinkingLabelText");
    c.postsTabText = ts.translate("initBase", "Posts and threads", "postsTabText");
    c.quickReplyActionAppendPostText = ts.translate("initBase", "Appends a new post",
                                                    "quickReplyActionAppendPostText");
    c.quickReplyActionDoNothingText = ts.translate("initBase", "Leaves page unmodified",
                                                   "quickReplyActionDoNothingText");
    c.quickReplyActionGotoThreadText = ts.translate("initBase", "Redirects to thread",
                                                    "quickReplyActionGotoThreadText");
    c.quickReplyActionLabelText = ts.translate("initBase", "Quick reply outside thread:", "quickReplyActionLabelText");
    c.ratingTooltip = ts.translate("initBase", "SFW - safe for work (no socially condemned content)\n"
                                   "R-15 - restricted for 15 years (contains ecchi, idols, violence)\n"
                                   "R-18 - restricted for 18 years (genitalis, coitus, offensive "
                                   "religious/racist/nationalist content)\n"
                                   "R-18G - restricted for 18 years, guidance advised (shemale, death, guro, scat, "
                                   "defecation, urination, etc.)", "ratingTooltip");
    c.rememberAudioVideoVolumeLabelText = ts.translate("initBase", "Remember volume:",
                                                       "rememberAudioVideoVolumeLabelText");
    c.removeFromFavoritesText = ts.translate("initBase", "Remove from favorites", "removeFromFavoritesText");
    c.removeFromHiddenPostListText = ts.translate("initBase", "Remove from hidden post/thread list",
                                                  "removeFromHiddenPostListText");
    c.scriptSettingsLegendText = ts.translate("initBase", "Script settings", "scriptSettingsLegendText");
    SettingsLocker s;
    c.searchButtonText = ts.translate("initBase", "Search", "searchButtonText");
    c.searchInputPlaceholder = ts.translate("initBase", "Search: possible +required -excluded",
                                            "searchInputPlaceholder");
    c.settingsButtonText = ts.translate("initBase", "Settings", "settingsButtonText");
    c.settingsDialogTitle = ts.translate("initBase", "Settings", "settingsDialogTitle");
    c.showAttachedFilePreviewLabelText = ts.translate("initBase", "Show previews when attaching files:",
                                                      "showAttachedFilePreviewLabelText");
    c.showAutoUpdateDesktopNotificationsLabelText = ts.translate("initBase", "Show desktop notifications:",
                                                                 "showAutoUpdateDesktopNotificationsLabelText");
    c.showAutoUpdateTimerLabelText = ts.translate("initBase", "Show auto update timer:",
                                                  "showAutoUpdateTimerLabelText");
    c.showFavoriteText = ts.translate("initBase", "Favorites", "showFavoriteText");
    c.showHiddenPostListText = ts.translate("initBase", "Show hidden post/thread list", "showHiddenPostListText");
    c.showLeafButtonsLabelText = ts.translate("initBase", "Show file leaf buttons:", "showLeafButtonsLabelText");
    c.showNewPostsLabelText = ts.translate("initBase", "Show new post count near board names:",
                                           "showNewPostsLabelText");
    c.showPasswordText = ts.translate("initBase", "Show password", "showPasswordText");
    c.showYoutubeVideoTitleLabelText = ts.translate("initBase", "Show titles of YouTube videos:",
                                                    "showYoutubeVideoTitleLabelText");
    c.shrinkPosts = !Tools::cookieValue(req, "shrinkPosts").compare("true", Qt::CaseInsensitive);
    c.shrinkPostsClass = c.shrinkPosts ? " shrinkedPost" : "";
    c.signOpPostLinksLabelText = ts.translate("initBase", "Mark OP post links:", "signOpPostLinksLabelText");
    c.signOwnPostLinksLabelText = ts.translate("initBase", "Mark own post links:", "signOwnPostLinksLabelText");
    c.siteDomain = Tools::toStd(s->value("Site/domain").toString());
    c.sitePathPrefix = Tools::toStd(s->value("Site/path_prefix").toString());
    c.siteProtocol = Tools::toStd(s->value("Site/protocol").toString());
    if (c.siteProtocol.empty())
        c.siteProtocol = "http";
    c.spellsLabelText = ts.translate("initBase", "Spells (command-based post hiding):", "spellsLabelText");
    c.strikeOutHiddenPostLinksLabelText = ts.translate("initBase", "Strike out links to hidden posts:",
                                                       "strikeOutHiddenPostLinksLabelText");
    c.stripExifFromJpegLabelText = ts.translate("initBase", "Strip EXIF from JPEG files:",
                                                "stripExifFromJpegLabelText");
    c.style.name = Tools::toStd(Tools::cookieValue(req, "style"));
    if (c.style.name.empty())
        c.style.name = "photon";
    t = styles.value(Tools::fromStd(c.style.name));
    c.style.title = ts.translate(t.context().toUtf8().constData(), t.sourceText().toUtf8().constData(),
                                 t.disambiguation().toUtf8().constData());
    c.styleLabelText = ts.translate("initBase", "Style:", "styleLabelText");
    foreach (const QString &s, styles.keys()) {
        Content::Base::Style st;
        st.name = Tools::toStd(s);
        t = styles.value(s);
        st.title = ts.translate(t.context().toUtf8().constData(), t.sourceText().toUtf8().constData(),
                                t.disambiguation().toUtf8().constData());
        c.styles.push_back(st);
    }
    c.timeLabelText = ts.translate("initBase", "Time:", "timeLabelText");
    c.timeLocalText = ts.translate("initBase", "Local", "timeLocalText");
    c.timeServerText = ts.translate("initBase", "Server", "timeServerText");
    c.timeZoneOffset = Tools::cookieValue(req, "timeZoneOffset").toInt();
    c.timeZoneOffsetLabelText = ts.translate("initBase", "Offset:", "timeZoneOffsetLabelText");
    c.toFaqPageText = ts.translate("initBase", "F.A.Q.", "toFaqPageText");
    c.toHomePageText = ts.translate("initBase", "Home", "toHomePageText");
    c.toManagePageText = ts.translate("initBase", "User management", "toManagePageText");
    c.toPlaylistPageText = ts.translate("initBase", "Playlist", "toPlaylistPageText");
    c.toMarkupPageText = ts.translate("initBase", "Markup", "toMarkupPageText");
    c.userCssLabelText = ts.translate("initBase", "User CSS:", "userCssLabelText");
}

bool initBaseBoard(Content::BaseBoard &c, const cppcms::http::request &req, const AbstractBoard *board,
                   bool postingEnabled, const QString &pageTitle, quint64 currentThread)
{
    if (!board)
        return false;
    TranslatorStd ts(req);
    TranslatorQt tq(req);
    initBase(c, req, pageTitle);
    if (c.pageTitle.empty() && currentThread)
        c.pageTitle = Tools::toStd(board->title(ts.locale()) + " - " + QString::number(currentThread));
    QStringList userBoards = Database::registeredUserBoards(req);
    if (userBoards.size() == 1 && userBoards.first() == "*")
        userBoards << AbstractBoard::boardNames();
    foreach (const QString &s, userBoards) {
        AbstractBoard::BoardInfo inf;
        inf.name = Tools::toStd(s);
        AbstractBoard::LockingWrapper b = AbstractBoard::board(s);
        inf.title = Tools::toStd(b ? b->title(tq.locale()) : tq.translate("initBaseBoard", "All boards", "boardName"));
        c.availableBoards.push_back(inf);
    }
    c.action = currentThread ? "create_post" : "create_thread";
    c.addFileText = ts.translate("initBaseBoard", "Add file", "addFileText");
    c.addToPlaylistText = ts.translate("initBaseBoard", "Add to playlist", "addToPlaylistText");
    c.addThreadToFavoritesText = ts.translate("initBaseBoard", "Add thread to favorites", "addThreadToFavoritesText");
    c.ajaxErrorText = ts.translate("initBaseBoard", "AJAX request returned status", "ajaxErrorText");
    c.attachFileByLinkText = ts.translate("initBaseBoard", "Specify file URL", "attachFileByLinkText");
    c.audioTagAlbumText = ts.translate("initBaseBoard", "Album:", "audioTagAlbumText");
    c.audioTagArtistText = ts.translate("initBaseBoard", "Artist:", "audioTagArtistText");
    c.audioTagTitleText = ts.translate("initBaseBoard", "Title:", "audioTagTitleText");
    c.audioTagYearText = ts.translate("initBaseBoard", "Year:", "audioTagYearText");
    foreach (const QString &id, Tools::supportedCodeLanguages()) {
        Content::BaseBoard::Lang lang;
        lang.id = Tools::toStd(id);
        QString name = Tools::langName(id);
        if (!name.isEmpty() && "-" != id)
            name += " (" + id + ")";
        else
            name = id;
        lang.name = Tools::toStd(name);
        c.availableLangs.push_back(lang);
    }
    c.banExpiresLabelText = ts.translate("initBaseBoard", "Expiration time:", "banExpiresLabelText");
    c.banLevelLabelText = ts.translate("initBaseBoard", "Level:", "banLevelLabelText");
    Content::BanLevel bl;
    bl.level = 0;
    bl.description = ts.translate("initBaseBoard", "Not banned", "banLevelDesctiption");
    c.banLevels.push_back(bl);
    bl.level = 1;
    bl.description = ts.translate("initBaseBoard", "Posting prohibited", "banLevelDesctiption");
    c.banLevels.push_back(bl);
    bl.level = 10;
    bl.description = ts.translate("initBaseBoard", "Posting and reading prohibited", "banLevelDesctiption");
    c.banLevels.push_back(bl);
    c.bannedForText = ts.translate("initBaseBoard", "User was banned for this post", "bannedForText");
    c.bannerFileName = Tools::toStd(board->bannerFileName());
    c.banReasonLabelText = ts.translate("initBaseBoard", "Reason:", "banReasonLabelText");
    c.banUserText = ts.translate("initBaseBoard", "Ban user", "banUserText");
    c.boardLabelText = ts.translate("initBaseBoard", "Board:", "boardLabelText");
    c.bytesText = ts.translate("initBaseBoard", "Byte(s)", "bytesText");
    c.bumpLimit = board->bumpLimit();
    c.bumpLimitReachedText = ts.translate("initBaseBoard", "Bump limit reached", "bumpLimitReachedText");
    QString ip = Tools::userIp(req);
    c.captchaEnabled = Tools::captchaEnabled(board->name());
    QStringList supportedCaptchaEngines = board->supportedCaptchaEngines().split(',');
    if (supportedCaptchaEngines.isEmpty())
        return false;
    QString ceid = Tools::cookieValue(req, "captchaEngine");
    if (ceid.isEmpty() || !supportedCaptchaEngines.contains(ceid, Qt::CaseInsensitive)) {
        if (supportedCaptchaEngines.contains("google-recaptcha"))
            ceid = "google-recaptcha";
        else
            ceid = supportedCaptchaEngines.first();
    }
    AbstractCaptchaEngine::LockingWrapper ce = AbstractCaptchaEngine::engine(ceid);
    if (ce.isNull())
        return false;
    bool asceticMode = ("ascetic" == c.mode.name);
    c.captchaHeaderHtml = Tools::toStd(ce->headerHtml(asceticMode));
    c.captchaScriptSource = Tools::toStd(ce->scriptSource(asceticMode));
    c.captchaWidgetHtml = Tools::toStd(ce->widgetHtml(req, asceticMode));
    c.captchaQuota = board->captchaQuota(ip);
    c.captchaQuotaText = ts.translate("initBaseBoard", "Posts left:", "captchaQuotaText");
    c.closedText = ts.translate("initBaseBoard", "The thread is closed", "closedText");
    c.closeThreadText = ts.translate("initBaseBoard", "Close thread", "closeThreadText");
    c.collapseVideoText = ts.translate("initBaseBoard", "Collapse video", "collapseVideoText");
    c.complainText = ts.translate("initBaseBoard", "Complain", "complainText");
    c.complainMessage = ts.translate("initBaseBoard", "Go complain to your mum, you whiner!", "complainMessage");
    c.currentBoard.name = Tools::toStd(board->name());
    c.currentBoard.title = Tools::toStd(board->title(ts.locale()));
    QMap<QString, Content::BaseBoard::MarkupMode> mmmap;
    Content::BaseBoard::MarkupMode mm;
    mm.name = "none";
    mm.title = ts.translate("initBaseBoard", "No markup", "markupMode name");
    mmmap.insert(Tools::fromStd(mm.name), mm);
    c.markupModes.push_back(mm);
    mm.name = "ewm_only";
    mm.title = ts.translate("initBaseBoard", "Extended WakabaMark only", "markupMode name");
    mmmap.insert(Tools::fromStd(mm.name), mm);
    c.markupModes.push_back(mm);
    mm.name = "bbc_only";
    mm.title = ts.translate("initBaseBoard", "bbCode only", "markupMode name");
    mmmap.insert(Tools::fromStd(mm.name), mm);
    c.markupModes.push_back(mm);
    mm.name = "ewm_and_bbc";
    mm.title = ts.translate("initBaseBoard", "Extended WakabaMark and bbCode", "markupMode name");
    mmmap.insert(Tools::fromStd(mm.name), mm);
    c.markupModes.push_back(mm);
    QString mmc = Tools::cookieValue(req, "markupMode");
    if (mmc.isEmpty())
        mmc = "ewm_and_bbc";
    c.currentMarkupMode = mmmap.value(mmc);
    c.currentThread = currentThread;
    c.delallButtonText = ts.translate("initBaseBoard", "Delete all user posts on selected board", "delallButtonText");
    c.deleteFileText = ts.translate("initBaseBoard", "Delete file", "deleteFileText");
    c.deletePostText = ts.translate("initBaseBoard", "Delete post", "deletePostText");
    c.deleteThreadText = ts.translate("initBaseBoard", "Delete thread", "deleteThreadText");
    c.downloadThreadText = ts.translate("initBaseBoard", "Download all thread files as a .zip archive",
                                        "downloadThreadText");
    c.draftsEnabled = board->draftsEnabled();
    c.draftText = ts.translate("initBaseBoard", "Draft", "draftText");
    c.editAudioTagsText = ts.translate("initBaseBoard", "Edit audio file tags", "editAudioTagsText");
    c.editPostText = ts.translate("initBaseBoard", "Edit post", "editPostText");
    c.enterPasswordText = ts.translate("initBaseBoard", "If password is empty, current hashpass will be used",
                                       "enterPasswordText");
    c.enterPasswordTitle = ts.translate("initBaseBoard", "Enter password", "enterPasswordTitle");
    c.expandVideoText = ts.translate("initBaseBoard", "Expand video", "expandVideoText");
    c.fileExistsOnServerText = ts.translate("initBaseBoard", "This file exists on server. "
                                            "It will NOT be uploaded, but WILL be copied.", "fileExistsOnServerText");
    c.fileTooLargeWarningText = ts.translate("initBaseBoard", "Selected file is too large", "fileTooLargeWarningText");
    c.findSourceWithGoogleText = ts.translate("initBaseBoard", "Find source with Google", "findSourceWithGoogleText");
    c.findSourceWithIqdbText = ts.translate("initBaseBoard", "Find source with Iqdb", "findSourceWithIqdbText");
    c.fixedText = ts.translate("initBaseBoard", "Fixed", "fixedText");
    c.fixThreadText = ts.translate("initBaseBoard", "Fix thread", "fixThreadText");
    c.hideByImageText = ts.translate("initBaseBoard", "Hide by image hash", "hideByImageText");
    c.hidePostformMarkupText = ts.translate("initBaseBoard", "Hide markup", "hidePostformMarkupText");
    c.hidePostformRulesText = ts.translate("initBaseBoard", "Hide rules", "hidePostformRulesText");
    c.hidePostFormText = ts.translate("initBaseBoard", "Hide post form", "hidePostFormText");
    c.internalErrorText = ts.translate("initBaseBoard", "Internal error", "internalErrorText");
    c.kilobytesText = ts.translate("initBaseBoard", "KB", "kilobytesText");
    c.linkLabelText = ts.translate("initBaseBoard", "URL:", "linkLabelText");
    c.loadingPostsText = ts.translate("initBaseBoard", "Loading posts...", "loadingPostsText");
    c.markupBold = ts.translate("initBaseBoard", "Bold text", "markupBold");
    c.markupCode = ts.translate("initBaseBoard", "Code block", "markupCode");
    c.markupElements = board->markupElements();
    c.markupItalics = ts.translate("initBaseBoard", "Italics", "markupItalics");
    c.markupLang = ts.translate("initBaseBoard", "Code block syntax", "markupLang");
    c.markupQuotation = ts.translate("initBaseBoard", "Quote selected text", "markupQuotation");
    c.markupSpoiler = ts.translate("initBaseBoard", "Spoiler", "markupSpoiler");
    c.markupStrikedOut = ts.translate("initBaseBoard", "Striked out text", "markupStrikedOut");
    c.markupSubscript = ts.translate("initBaseBoard", "Subscript", "markupSubscript");
    c.markupSuperscript = ts.translate("initBaseBoard", "Superscript", "markupSuperscript");
    c.markupUnderlined = ts.translate("initBaseBoard", "Underlined text", "markupUnderlined");
    c.markupUrl = ts.translate("initBaseBoard", "URL (external link)", "markupUrl");
    c.maxEmailLength = Tools::maxInfo(Tools::MaxEmailFieldLength, board->name());
    c.maxFileCount = Tools::maxInfo(Tools::MaxFileCount, board->name());
    c.maxFileSize = Tools::maxInfo(Tools::MaxFileSize, board->name());
    c.maxNameLength = Tools::maxInfo(Tools::MaxNameFieldLength, board->name());
    c.maxSubjectLength = Tools::maxInfo(Tools::MaxSubjectFieldLength, board->name());
    c.maxPasswordLength = Tools::maxInfo(Tools::MaxPasswordFieldLength, board->name());
    c.maxTextLength = Tools::maxInfo(Tools::MaxTextFieldLength, board->name());
    c.megabytesText = ts.translate("initBaseBoard", "MB", "megabytesText");
    if (c.moder > 0) {
        QStringList boards = Database::registeredUserBoards(req);
        if (!boards.contains("*") && !boards.contains(board->name()))
            c.moder = 0;
    }
    c.modificationDateTimeText = ts.translate("initBaseBoard", "Last modified:", "modificationDateTimeText");
    c.moveThreadText = ts.translate("initBaseBoard", "Move thread", "moveThreadText");
    c.moveThreadWarningText = ts.translate("initBaseBoard", "Warning: post numbers will be changed, and so will the "
                                           "post references. But the raw post text will not bechanged, so be careful "
                                           "when editing posts in moved thread.", "moveThreadWarningText");
    c.nextFileText = ts.translate("initBaseBoard", "Next file", "nextFileText");
    c.noCaptchaText = ts.translate("initBaseBoard", "You don't have to enter captcha", "noCaptchaText");
    c.notLoggedInText = ts.translate("initBaseBoard", "You are not logged in!", "notLoggedInText");
    c.noTokenInTableErrorText = ts.translate("initBaseBoard", "Invalid spell order", "noTokenInTableErrorText");
    c.openThreadText = ts.translate("initBaseBoard", "Open thread", "openThreadText");
    c.postActionsText = ts.translate("initBaseBoard", "Post actions", "postActionsText");
    c.postFormButtonSubmit = ts.translate("initBaseBoard", "Send", "postFormButtonSubmit");
    c.postFormButtonSubmitSending = ts.translate("initBaseBoard", "Sending:", "postFormButtonSubmitSending");
    c.postFormButtonSubmitWaiting = ts.translate("initBaseBoard", "Waiting for reply...",
                                                 "postFormButtonSubmitWaiting");
    c.postFormInputFile = ts.translate("initBaseBoard", "File(s):", "postFormInputFile");
    SettingsLocker s;
    int maxText = s->value("Board/" + board->name() + "/max_text_length",
                           s->value("Board/max_text_length", 15000)).toInt();
    c.postFormTextPlaceholder = Tools::toStd(tq.translate("initBaseBoard", "Comment. Max length %1",
                                                           "postFormTextPlaceholder").arg(maxText));
    c.postFormLabelCaptcha = ts.translate("initBaseBoard", "Captcha:", "postFormLabelCaptcha");
    c.postFormLabelDraft = ts.translate("initBaseBoard", "Draft:", "postFormLabelDraft");
    c.postFormLabelEmail = ts.translate("initBaseBoard", "E-mail:", "postFormLabelEmail");
    c.postFormLabelMarkupMode = ts.translate("initBaseBoard", "Markup mode:", "postFormLabelMarkupMode");
    c.postFormLabelName = ts.translate("initBaseBoard", "Name:", "postFormLabelName");
    c.postFormLabelOptions = ts.translate("initBaseBoard", "Options:", "postFormLabelOptions");
    c.postFormLabelPassword = ts.translate("initBaseBoard", "Password:", "postFormLabelPassword");
    c.postFormLabelRaw = ts.translate("initBaseBoard", "Raw HTML:", "postFormLabelRaw");
    c.postFormLabelSignAsOp = ts.translate("initBaseBoard", "OP:", "postFormLabelSignAsOp");
    c.postFormLabelSubject = ts.translate("initBaseBoard", "Subject:", "postFormLabelSubject");
    c.postFormLabelText = ts.translate("initBaseBoard", "Post:", "postFormLabelText");
    c.postFormLabelTripcode = ts.translate("initBaseBoard", "Tripcode:", "postFormLabelTripcode");
    c.postFormPlaceholderEmail = ts.translate("initBaseBoard", "E-mail", "postFormPlaceholderEmail");
    c.postFormPlaceholderName = ts.translate("initBaseBoard", "Name", "postFormPlaceholderName");
    c.postFormPlaceholderPassword = ts.translate("initBaseBoard", "Password", "postFormPlaceholderPassword");
    c.postFormPlaceholderSubject = ts.translate("initBaseBoard", "Subject", "postFormPlaceholderSubject");
    c.postFormTooltipDraft = ts.translate("initBaseBoard", "A post marked as a draft is only visible to yourself and "
                                          "moderators/admins. You may edit a draft as many times as you wish. "
                                          "Finally, when you remove the \"draft\" mark, your post will become visible "
                                          "to everybody, and you will not be able to edit it anymore.\n"
                                          "Note, that you may also remove attached files or attach new ones while a "
                                          "post is marked as a draft.\n"
                                          "You have to be logged in (NO registration, only a browser cookie!) to use "
                                          "drafts. You may edit your drafts from any browser and any device if you "
                                          "are logged in with the same password.", "postFormTooltipDraft");
    c.postingDisabledText = currentThread
            ? ts.translate("initBaseBoard", "Posting is disabled for this thread", "postingDisabledText")
            : ts.translate("initBaseBoard", "Posting is disabled for this board", "postingDisabledText");
    c.postingEnabled = postingEnabled;
    c.postingSpeedText = ts.translate("initBaseBoard", "Posting speed:", "postingSpeedText");
    AbstractBoard::PostingSpeed speed = board->postingSpeed();
    double duptime = double(speed.uptimeMsecs) / double(BeQt::Hour);
    qint64 uptime = qint64(duptime);
    std::string shour = ts.translate("initBaseBoard", "post(s) per hour.", "postingSpeed");
    if (!uptime) {
        c.postingSpeed = zeroSpeedString(speed, shour, ts.locale());
    } else if ((speed.postCount / uptime) > 0) {
        c.postingSpeed = speedString(speed, duptime) + " " + shour;
    } else {
        duptime /= 24.0;
        uptime = qint64(duptime);
        std::string sday = ts.translate("initBaseBoard", "post(s) per day.", "postingSpeed");
        if (!uptime) {
            c.postingSpeed = zeroSpeedString(speed, sday, ts.locale());
        } else if ((speed.postCount / uptime) > 0) {
            c.postingSpeed = speedString(speed, duptime) + " " + sday;
        } else {
            duptime /= (365.0 / 12.0);
            uptime = qint64(duptime);
            std::string smonth = ts.translate("initBaseBoard", "post(s) per month.", "postingSpeed");
            if (!uptime) {
                c.postingSpeed = zeroSpeedString(speed, smonth, ts.locale());
            } else if ((speed.postCount / uptime) > 0) {
                c.postingSpeed = speedString(speed, duptime) + " " + smonth;
            } else {
                duptime /= 12.0;
                uptime = qint64(duptime);
                std::string syear = ts.translate("initBaseBoard", "post(s) per year.", "postingSpeed");
                if (!uptime) {
                    c.postingSpeed = zeroSpeedString(speed, syear, ts.locale());
                } else if ((speed.postCount / uptime) > 0) {
                    c.postingSpeed = speedString(speed, duptime) + " " + syear;
                } else {
                    c.postingSpeed = "0 " + syear;
                }
            }
        }
    }
    c.postLimit = board->postLimit();
    c.postLimitReachedText = ts.translate("initBaseBoard", "Post limit reached", "postLimitReachedText");
    foreach (QString r, board->postformRules(tq.locale()))
        c.postformRules.push_back(Tools::toStd(r.replace("%currentBoard.name%", board->name())));
    c.previousFileText = ts.translate("initBaseBoard", "Previous file", "previousFileText");
    c.quickReplyText = ts.translate("initBaseBoard", "Quick reply", "quickReplyText");
    c.ratingLabelText = ts.translate("initBaseBoard", "Rating:", "ratingLabelText");
    c.rawPostTextText = ts.translate("initBaseBoard", "Raw post text", "rawPostTextText");
    c.referencedByText = ts.translate("initBaseBoard", "Answers:", "referencedByText");
    c.registeredText = ts.translate("initBaseBoard", "This user is registered", "registeredText");
    c.removeFileText = ts.translate("initBaseBoard", "Remove this file", "removeFileText");
    c.selectAllText = ts.translate("initBaseBoard", "Select all", "selectAllText");
    c.selectFileText = ts.translate("initBaseBoard", "Select file", "selectFileText");
    c.showPostformMarkupText = ts.translate("initBaseBoard", "Show markup", "showPostformMarkupText");
    c.showPostformRulesText = ts.translate("initBaseBoard", "Show rules", "showPostformRulesText");
    c.showPostFormText = currentThread ? ts.translate("initBaseBoard", "Answer in this thread", "showPostFormText")
                                       : ts.translate("initBaseBoard", "Create thread", "showPostFormText");
    c.showHidePostText = ts.translate("initBaseBoard", "Hide/show", "showHidePostText");
    c.showUserIpText = ts.translate("initBaseBoard", "Show user IP", "showUserIpText");
    c.showWhois = board->showWhois();
    c.supportedFileTypes = Tools::toStd(board->supportedFileTypes());
    c.toBottomText = ts.translate("initBaseBoard", "Scroll to the bottom", "toBottomText");
    c.toThread = ts.translate("initBaseBoard", "Answer", "toThread");
    c.toTopText = ts.translate("initBaseBoard", "Scroll to the top", "toTopText");
    c.unexpectedEndOfTokenListErrorText = ts.translate("initBaseBoard", "Unexpected end of spell list",
                                                       "unexpectedEndOfTokenListErrorText");
    c.unfixThreadText = ts.translate("initBaseBoard", "Unfix thread", "unfixThreadText");
    c.unselectAllText = ts.translate("initBaseBoard", "Unselect all", "unselectAllText");
    c.youtubeApiKey = Tools::toStd(s->value("Site/youtube_api_key").toString());
    return true;
}

void redirect(cppcms::application &app, const QString &where)
{
    app.response().set_redirect_header(Tools::toStd(where));
}

void renderBan(cppcms::application &app, const Database::BanInfo &info)
{
    if (shouldBeAjax(app))
        renderBanAjax(app, info);
    else
        renderBanNonAjax(app, info);
}

void renderBanAjax(cppcms::application &app, const Database::BanInfo &info)
{
    TranslatorStd ts(app.request());
    cppcms::json::object o;
    o["errorMessage"] = ts.translate("renderBanAjax", "You are banned", "errorMessage");
    std::string desc = ts.translate("renderBanAjax", "Board:", "errorDescription") + " ";
    desc += (("*" != info.boardName) ? Tools::toStd(info.boardName)
                                     : ts.translate("renderBanAjax", "all boards", "errorDescription")) + ". ";
    desc += ts.translate("renderBanAjax", "Date:", "errorDescription") + " ";
    desc += Tools::toStd(ts.locale().toString(Tools::dateTime(info.dateTime, app.request()),
                                              "dd.MM.yyyy ddd hh:mm:ss")) + ". ";
    desc += ts.translate("renderBanAjax", "Expires:", "errorDescription") + " ";
    desc += (info.expires.isValid() ? Tools::toStd(ts.locale().toString(Tools::dateTime(info.expires, app.request()),
                                                                       "dd.MM.yyyy ddd hh:mm:ss"))
                                    : ts.translate("renderBanAjax", "never", "errorDescription")) + ". ";
    desc += ts.translate("renderBanAjax", "Restricted actions:", "errorDescription") + " ";
    if (info.level >= 10)
        desc += ts.translate("renderBanAjax", "reading and posting are restricted", "errorDescription");
    else if (info.level >= 1)
        desc += ts.translate("renderBanAjax", "posting is restricted (read-only access)", "errorDescription");
    else
        desc += ts.translate("renderBanAjax", "no action is restricted", "errorDescription");
    desc += ". " + ts.translate("renderBanAjax", "Reason:", "banReasonLabel") + " " + Tools::toStd(info.reason);
    o["errorDescription"] = desc;
    app.response().out() << cppcms::json::value(o).save();
}

void renderBanNonAjax(cppcms::application &app, const Database::BanInfo &info)
{
    TranslatorQt tq(app.request());
    TranslatorStd ts(app.request());
    Content::Ban c;
    initBase(c, app.request(), tq.translate("renderBan", "Ban", "banBoard"));
    c.banBoard = ("*" != info.boardName) ? Tools::toStd(info.boardName)
                                         : ts.translate("renderBan", "all boards", "pageTitle");
    c.banBoardLabel = ts.translate("renderBan", "Board", "banBoardLabel");
    c.banDateTime = Tools::toStd(ts.locale().toString(Tools::dateTime(info.dateTime, app.request()),
                                                      "dd.MM.yyyy ddd hh:mm:ss"));
    c.banDateTimeLabel = ts.translate("renderBan", "Date", "banDateTimeLabel");
    c.banExpires = info.expires.isValid()
            ? Tools::toStd(ts.locale().toString(Tools::dateTime(info.expires, app.request()),
                                                "dd.MM.yyyy ddd hh:mm:ss"))
            : ts.translate("renderBan", "never", "banExpires");
    c.banExpiresLabel = ts.translate("renderBan", "Expires", "banExpiresLabel");
    if (info.level >= 10)
        c.banLevel = ts.translate("renderBan", "reading and posting are restricted", "pageTitle");
    else if (info.level >= 1)
        c.banLevel = ts.translate("renderBan", "posting is restricted (read-only access)", "pageTitle");
    else
        c.banLevel = ts.translate("renderBan", "no action is restricted", "pageTitle");
    c.banLevelLabel = ts.translate("renderBan", "Restricted actions", "banLevelLabel");
    c.banMessage = ts.translate("renderBan", "You are banned", "banMessage");
    c.banReason = Tools::toStd(info.reason);
    c.banReasonLabel = ts.translate("renderBan", "Reason", "banReasonLabel");
    Tools::render(app, "ban", c);
}

void renderError(cppcms::application &app, const QString &error, const QString &description)
{
    if (shouldBeAjax(app))
        renderErrorAjax(app, error, description);
    else
        renderErrorNonAjax(app, error, description);
}

void renderErrorAjax(cppcms::application &app, const QString &error, const QString &description)
{
    TranslatorStd ts(app.request());
    cppcms::json::object o;
    o["errorMessage"] = !error.isEmpty() ? Tools::toStd(error) : ts.translate("renderError", "Error", "errorMessage");
    o["errorDescription"] = Tools::toStd(description);
    app.response().out() << cppcms::json::value(o).save();
}

void renderErrorNonAjax(cppcms::application &app, const QString &error, const QString &description)
{
    TranslatorQt tq(app.request());
    Content::Error c;
    initBase(c, app.request(), tq.translate("renderError", "Error", "pageTitle"));
    c.errorMessage = !error.isEmpty() ? Tools::toStd(error) : c.pageTitle;
    c.errorDescription = Tools::toStd(description);
    Tools::render(app, "error", c);
}

void renderIpBan(cppcms::application &app, int level)
{
    if (shouldBeAjax(app))
        renderIpBanAjax(app, level);
    else
        renderIpBanNonAjax(app, level);
}

void renderIpBanAjax(cppcms::application &app, int level)
{
    TranslatorStd ts(app.request());
    cppcms::json::object o;
    o["errorMessage"] = ts.translate("renderIpBanAjax", "You are banned", "errorMessage");
    if (level >= 10) {
        o["errorDescription"] = ts.translate("renderIpBanAjax", "Your IP address is in the ban list. "
                                             "You are not allowed to read or make posts.", "errorDescription");
    } else if (level >= 1) {
        o["errorDescription"] = ts.translate("renderIpBanAjax", "Your IP address is in the ban list. "
                                              "You are not allowed to make posts.", "errorDescription");
    }
    app.response().out() << cppcms::json::value(o).save();
}

void renderIpBanNonAjax(cppcms::application &app, int level)
{
    TranslatorQt tq(app.request());
    TranslatorStd ts(app.request());
    Content::IpBan c;
    initBase(c, app.request(), tq.translate("renderIpBan", "Ban", "pageTitle"));
    c.banMessage = ts.translate("renderIpBan", "You are banned", "banMessage");
    if (level >= 10) {
        c.banDescription = ts.translate("renderIpBan", "Your IP address is in the ban list. "
                                        "You are not allowed to read or make posts.", "banDescription");
    } else if (level >= 1) {
        c.banDescription = ts.translate("renderIpBan", "Your IP address is in the ban list. "
                                        "You are not allowed to make posts.", "banDescription");
    }
    Tools::render(app, "ip_ban", c);
}

void renderNotFound(cppcms::application &app)
{
    if (shouldBeAjax(app))
        renderNotFoundAjax(app);
    else
        renderNotFoundNonAjax(app);
}

void renderNotFoundAjax(cppcms::application &app)
{
    TranslatorStd ts(app.request());
    cppcms::json::object o;
    o["errorMessage"] = ts.translate("renderNotFoundAjax", "Error 404", "errorMessage");
    o["errorDescription"] = ts.translate("renderNotFoundAjax", "Page or file not found", "errorDescription");
    QStringList fns;
    foreach (const QString &path, BCoreApplication::locations(BCoreApplication::DataPath))
        fns << QDir(path + "/static/img/not_found").entryList(QDir::Files);
    if (!fns.isEmpty()) {
        qsrand((uint) QDateTime::currentMSecsSinceEpoch());
        o["imageFileName"] = Tools::toStd("not_found/" + fns.at(qrand() % fns.size()));
    }
    app.response().out() << cppcms::json::value(o).save();
}

void renderNotFoundNonAjax(cppcms::application &app)
{
    TranslatorQt tq(app.request());
    TranslatorStd ts(app.request());
    Content::NotFound c;
    initBase(c, app.request(), tq.translate("renderNotFound", "Error 404", "pageTitle"));
    QStringList fns;
    foreach (const QString &path, BCoreApplication::locations(BCoreApplication::DataPath))
        fns << QDir(path + "/static/img/not_found").entryList(QDir::Files);
    if (!fns.isEmpty()) {
        qsrand((uint) QDateTime::currentMSecsSinceEpoch());
        c.imageFileName = Tools::toStd("not_found/" + fns.at(qrand() % fns.size()));
    }
    c.notFoundMessage = ts.translate("renderNotFound", "Page or file not found", "notFoundMessage");
    Tools::render(app, "not_found", c);
}

void renderSuccessfulPostAjax(cppcms::application &app, quint64 postNumber)
{
    cppcms::json::object o;
    o["postNumber"] = postNumber;
    app.response().out() << cppcms::json::value(o).save();
}

void renderSuccessfulThreadAjax(cppcms::application &app, quint64 threadNumber)
{
    cppcms::json::object o;
    o["threadNumber"] = threadNumber;
    app.response().out() << cppcms::json::value(o).save();
}

bool shouldBeAjax(cppcms::application &app)
{
    return Tools::cookieValue(app.request(), "mode").compare("ascetic", Qt::CaseInsensitive);
}

bool testAddFileParams(const AbstractBoard *board, cppcms::application &app, const Tools::PostParameters &params,
                       const Tools::FileList &files, QString *error)
{
    return shouldBeAjax(app) ? testAddFileParamsAjax(board, app, params, files, error)
                             : testAddFileParamsNonAjax(board, app, params, files, error);
}

bool testAddFileParamsAjax(const AbstractBoard *board, cppcms::application &app, const Tools::PostParameters &params,
                           const Tools::FileList &files, QString *error)
{
    TranslatorQt tq(app.request());
    if (!board) {
        QString err = tq.translate("testAddFileParamsAjax", "Internal logic error", "description");
        renderErrorAjax(app, tq.translate("testAddFileParamsAjax", "Internal error", "error"), err);
        return bRet(error, err, false);
    }
    QString err;
    if (!board->testAddFileParams(params, files, tq.locale(), &err)){
        renderErrorAjax(app, tq.translate("testAddFileParamsAjax", "Invalid parameters", "error"), err);
        return false;
    }
    return bRet(error, QString(), true);
}

bool testAddFileParamsNonAjax(const AbstractBoard *board, cppcms::application &app,
                              const Tools::PostParameters &params, const Tools::FileList &files, QString *error)
{
    TranslatorQt tq(app.request());
    if (!board) {
        QString err = tq.translate("testAddFileParamsAjax", "Internal logic error", "description");
        renderErrorNonAjax(app, tq.translate("testAddFileParamsAjax", "Internal error", "error"), err);
        return bRet(error, err, false);
    }
    QString err;
    if (!board->testAddFileParams(params, files, tq.locale(), &err)){
        renderErrorNonAjax(app, tq.translate("testAddFileParamsAjax", "Invalid parameters", "error"), err);
        return false;
    }
    return bRet(error, QString(), true);
}

bool testBan(cppcms::application &app, UserActionType proposedAction, const QString &board)
{
    return shouldBeAjax(app) ? testBanAjax(app, proposedAction, board) : testBanNonAjax(app, proposedAction, board);
}

bool testBanAjax(cppcms::application &app, UserActionType proposedAction, const QString &board)
{
    QString ip = Tools::userIp(app.request());
    int lvl = Tools::ipBanLevel(ip);
    if (lvl >= proposedAction) {
        renderIpBanAjax(app, lvl);
        return false;
    }
    TranslatorQt tq(app.request());
    bool ok = false;
    QString err;
    QMap<QString, Database::BanInfo> map = Database::userBanInfo(ip, &ok, &err, tq.locale());
    if (!ok) {
        renderErrorAjax(app, tq.translate("testBanAjax", "Internal error", "error"), err);
        return false;
    }
    if (!map.contains(board))
        return true;
    Database::BanInfo inf = map.value(board);
    if (inf.level >= proposedAction) {
        renderBanAjax(app, inf);
        return false;
    }
    return true;
}

bool testBanNonAjax(cppcms::application &app, UserActionType proposedAction, const QString &board)
{
    QString ip = Tools::userIp(app.request());
    int lvl = Tools::ipBanLevel(ip);
    if (lvl >= proposedAction) {
        renderIpBan(app, lvl);
        return false;
    }
    TranslatorQt tq(app.request());
    bool ok = false;
    QString err;
    QMap<QString, Database::BanInfo> map = Database::userBanInfo(ip, &ok, &err, tq.locale());
    if (!ok) {
        renderError(app, tq.translate("testBan", "Internal error", "error"), err);
        return false;
    }
    Database::BanInfo inf = map.value(board);
    if (inf.level >= proposedAction) {
        renderBan(app, inf);
        return false;
    }
    return true;
}

bool testParams(const AbstractBoard *board, cppcms::application &app, const Tools::PostParameters &params,
                const Tools::FileList &files, bool post, QString *error)
{
    return shouldBeAjax(app) ? testParamsAjax(board, app, params, files, post, error)
                             : testParamsNonAjax(board, app, params, files, post, error);
}

bool testParamsAjax(const AbstractBoard *board, cppcms::application &app, const Tools::PostParameters &params,
                    const Tools::FileList &files, bool post, QString *error)
{
    TranslatorQt tq(app.request());
    if (!board) {
        QString err = tq.translate("testParamsAjax", "Internal logic error", "description");
        renderErrorAjax(app, tq.translate("testParamsAjax", "Internal error", "error"), err);
        return bRet(error, err, false);
    }
    QString err;
    if (!board->testParams(params, files, post, tq.locale(), &err)){
        renderErrorAjax(app, tq.translate("testParamsAjax", "Invalid parameters", "error"), err);
        return false;
    }
    return bRet(error, QString(), true);
}

bool testParamsNonAjax(const AbstractBoard *board, cppcms::application &app, const Tools::PostParameters &params,
                       const Tools::FileList &files, bool post, QString *error)
{
    TranslatorQt tq(app.request());
    if (!board) {
        QString err = tq.translate("testParamsAjax", "Internal logic error", "description");
        renderErrorNonAjax(app, tq.translate("testParamsAjax", "Internal error", "error"), err);
        return bRet(error, err, false);
    }
    QString err;
    if (!board->testParams(params, files, post, tq.locale(), &err)){
        renderErrorNonAjax(app, tq.translate("testParamsAjax", "Invalid parameters", "error"), err);
        return false;
    }
    return bRet(error, QString(), true);
}

bool testRequest(cppcms::application &app, int acceptedTypes, QString *error)
{
    return shouldBeAjax(app) ? testRequestAjax(app, acceptedTypes, error)
                             : testRequestNonAjax(app, acceptedTypes, error);
}

bool testRequestAjax(cppcms::application &app, int acceptedTypes, QString *error)
{
    QString r = Tools::fromStd(app.request().request_method());
    bool b = acceptedTypes > 0;
    if ("GET" == r)
        b = b && (acceptedTypes & GetRequest);
    else if ("POST" == r)
        b = b && (acceptedTypes & PostRequest);
    if (b)
        return bRet(error, QString(), true);
    TranslatorQt tq(app.request());
    QString err = tq.translate("testRequest", "Unsupported request type", "error");
    renderErrorAjax(app, err, tq.translate("testRequest", "This request type is not supported", "error"));
    return bRet(error, err, false);
}

bool testRequestNonAjax(cppcms::application &app, int acceptedTypes, QString *error)
{
    QString r = Tools::fromStd(app.request().request_method());
    bool b = acceptedTypes > 0;
    if ("GET" == r)
        b = b && (acceptedTypes & GetRequest);
    else if ("POST" == r)
        b = b && (acceptedTypes & PostRequest);
    if (b)
        return bRet(error, QString(), true);
    TranslatorQt tq(app.request());
    QString err = tq.translate("testRequest", "Unsupported request type", "error");
    renderErrorNonAjax(app, err, tq.translate("testRequest", "This request type is not supported", "error"));
    return bRet(error, err, false);
}

}
