#include "vcall.h"

#include <pjsua2.hpp>

namespace voip {

class VAccount : public pj::Account
{
public:
    VCall *currentCall = nullptr;

    VAccount();

    ~VAccount();

    virtual void onRegState(pj::OnRegStateParam &prm) override;

    virtual void onIncomingCall(pj::OnIncomingCallParam &iprm) override;
};

} // namespace voip
