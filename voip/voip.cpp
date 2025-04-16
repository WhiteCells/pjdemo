#include <pjsua2.hpp>
#include <pj/limits.h>

#include <iostream>
#include <memory>

class SIPC {
public:
    SIPC() : ep(), acc() {
        init();
    }

    ~SIPC() {
        cleanup();
    }

    void registerAccount(
        const std::string &username,
        const std::string &password,
        const std::string &domain
    ) {
        std::cout << "--->" << __FUNCTION__ << std::endl;

        pj::AccountConfig acfg;
        acfg.idUri = "sip:" + username + "@" + domain;
        acfg.regConfig.registrarUri = "sip:" + domain;

        pj::AuthCredInfo cred("digest", "*", username, 0, password);
        acfg.sipConfig.authCreds.push_back(cred);

        acc.create(acfg);

        std::cout << "<---" << __FUNCTION__ << std::endl;
    }

    // 发起呼叫
    void makeCall(
        const std::string &username,
        const std::string &domain
    ) {
        std::cout << "--->" << __FUNCTION__ << std::endl;
        pj::CallOpParam param(false);
        call = std::make_unique<pj::Call>(acc, -1);
        std::string destination = "sip:" + username + domain;
        call->makeCall(destination, param);
        std::cout << "<---" << __FUNCTION__ << std::endl;
    }

    void hangupCall() {
        std::cout << "--->" << __FUNCTION__ << std::endl;
        if (call) {
            pj::CallOpParam param(false);
            call->hangup(param);
            std::cout << __FUNCTION__ << std::endl;
        }
        std::cout << "<---" << __FUNCTION__ << std::endl;
    }

private:
    void init() {
        pj::EpConfig ep_cfg;
        ep.libCreate();
        ep.libInit(ep_cfg);
        ep.libStart();
        // hanleAud();
    }

    void hanleAud() {
        auto cdevn = this->ep.audDevManager().getCaptureDev();
        auto cenum = this->ep.codecEnum2();
        for (auto &c : cenum) {
            std::cout << c.codecId << " " << c.desc << std::endl;
        }
    }

    void cleanup() {
        ep.libDestroy();
    }

    void onIncomingCall(pj::Call &call) {
        std::cout << "--->" << __FUNCTION__ << std::endl;
        std::cout << __FUNCTION__ << std::endl;
        pj::CallOpParam param(false);
        call.answer(param);
        std::cout << "<---" << __FUNCTION__ << std::endl;
    }

private:
    pj::Endpoint ep;
    pj::Account acc;
    std::unique_ptr<pj::Call> call;
};

int main() {
    SIPC c;

    c.registerAccount("1004", "1004", "192.168.10.51:5060");

    c.makeCall("another_user", "sip.example.com");

    std::cin.get();

    // 挂断通话
    c.hangupCall();

    return 0;
}
