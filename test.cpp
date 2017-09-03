#include "message.hpp"
#include "key.hpp"
#include "transaction.hpp"
#include "interpreter.hpp"
#include "chain.hpp"
#include "info.hpp"

#include "arcmist/base/log.hpp"


int main(int pArgumentCount, char **pArguments)
{
    int failed = 0;

    ArcMist::Log::setLevel(ArcMist::Log::DEBUG);

    if(!BitCoin::Base::test())
        failed++;

    if(!BitCoin::Key::test())
        failed++;

    if(!BitCoin::Transaction::test())
        failed++;

    if(!BitCoin::ScriptInterpreter::test())
        failed++;

    if(!BitCoin::Chain::test())
        failed++;

    if(!BitCoin::Message::test())
        failed++;

    if(!BitCoin::Info::test())
        failed++;

    if(failed)
        return 1;
    else
        return 0;
}
