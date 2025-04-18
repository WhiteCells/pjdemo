#include <pjsua2.hpp>

namespace voip {

class VCall;

class VAccount : public pj::Account
{
public:
    VAccount();

    ~VAccount();

    virtual void onRegState(pj::OnRegStateParam &prm) override;

    virtual void onIncomingCall(pj::OnIncomingCallParam &iprm) override;

    VCall *cur_call = nullptr;
};

} // namespace voip
