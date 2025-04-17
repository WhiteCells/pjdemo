#ifndef _VCALL_H_
#define _VCALL_H_

#include <pjsua2.hpp>

class VAccount;

namespace voip {

class VCall : public pj::Call
{

public:
    VCall(VAccount &acc, int call_id);
    ~VCall();

    virtual void onCallState(pj::OnCallStateParam &prm);
    virtual void onCallMediaState(pj::OnCallMediaStateParam &prm);
    // virtual void onDtmfDigit(OnDtmfDigitParam &prm); // Optional DTMF

private:
    VAccount &myAcc;
};

} // namespace voip

#endif // _VCALL_H_
