#ifndef _VMEDIA_H_
#define _VMEDIA_H_

#include <pjsua2.hpp>

namespace voip {

class VAudMedia : public pj::AudioMedia
{
public:
    VAudMedia(pjmedia_type med_type);
    ~VAudMedia();

    void handle()
    {
    }
};

} // namespace voip

#endif