#ifndef _VCALL_H_
#define _VCALL_H_

#include <pjsua2.hpp>

namespace voip {

class VAccount;

class VCall : public pj::Call
{
public:
    VCall(VAccount &acc, int call_id = PJSUA_INVALID_ID);
    ~VCall();

    virtual void onCallState(pj::OnCallStateParam &prm) override;
    virtual void onCallMediaState(pj::OnCallMediaStateParam &prm) override;
    // virtual void onStreamCreated(pj::OnStreamCreatedParam &prm) override;

private:
    VAccount &acc_;
};

} // namespace voip

#endif // _VCALL_H_
