#include "vaccount.h"
#include <iostream>

voip::VAccount::VAccount()
{
}

voip::VAccount::~VAccount()
{
}

void voip::VAccount::onRegState(pj::OnRegStateParam &prm)
{
    pj::AccountInfo ai = getInfo();
    std::cout << (ai.regIsActive ? "*** Registered:" : "*** Unregistered:")
              << " code=" << prm.code << " reason=" << prm.reason
              << " (" << ai.uri << ")" << std::endl;
}

void voip::VAccount::onIncomingCall(pj::OnIncomingCallParam &prm)
{
    
}