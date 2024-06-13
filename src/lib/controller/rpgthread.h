#ifndef CONTENT_RPGTHREAD_H
#define CONTENT_RPGTHREAD_H

#include "controller/thread.h"

#include "../global.h"

#include <string>

namespace Content
{

struct OLOLORD_EXPORT rpgThread : public Thread
{
    std::string addVoteVariantText;
    std::string closeVoteActionText;
    std::string multipleVoteVariantsText;
    std::string openVoteActionText;
    std::string removeVoteVariantText;
    std::string postFormLabelVote;
    std::string unvoteActionText;
    unsigned long long userIp;
    bool voteEnabled;
    std::string voteActionText;
    std::string voteClosedText;
    std::string votedText;
    std::string voteTextText;
};

}

#endif // CONTENT_RPGTHREAD_H
